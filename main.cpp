#include "PineVM.h"
#include "PineScript/PineCompiler.h" // 新增：PineScript 编译器头文件
#include "DataSource.h" // 新增：数据源层
#include "DataSource/CSVDataSource.h" // 新增：CSV数据源
#include <chrono> // 新增：用于时间测量
#include "DataSource/JsonDataSource.h" // 新增：JSON数据源
#include <iostream>
#include <vector>
#include <variant>
#include <iomanip>
#include <fstream>
#include "EasyLanguage/EasyLanguageLexer.h"
#include "EasyLanguage/EasyLanguageParser.h"
#include "EasyLanguage/EasyLanguageCompiler.h"
#include "Hithink/HithinkCompiler.h"
#include <thread>         // for std::thread
#include <mutex>          // for std::mutex and std::unique_lock
#include <condition_variable> // for std::condition_variable
#include <chrono>         // for std::chrono::milliseconds
#include <atomic>         // for std::atomic<bool>

// --- 全局共享资源和同步工具 ---
std::mutex data_mutex;              // 互斥锁，保护共享数据
std::condition_variable cv;         // 条件变量，用于线程间通信
std::atomic<bool> shutdown_flag{false}; // 原子布尔值，用于安全地停止生产者线程
int bars_produced = 0;              // 记录生产者已经生产了多少根K线 (受互斥锁保护)

// 辅助函数：向共享的Series中添加一个数据点 (线程安全)
int load_and_set_data_point(PineVM& vm) {
    int index = vm.getCurrentBarIndex() + 1;
    // 假设这是从某个实时数据源获取的数据
    double open = 100.0 + index;
    double high = open + 5.0;
    double low = open - 2.0;
    double close = open + 2.0;
    time_t timestamp = 1672531200 + index * 3600; // 模拟小时线

    vm.getSeries("open")->setCurrent(index, open);
    vm.getSeries("high")->setCurrent(index, high);
    vm.getSeries("low")->setCurrent(index, low);
    vm.getSeries("close")->setCurrent(index, close);
    vm.getSeries("time")->setCurrent(index, timestamp);

    return index;
}

// 生产者线程函数：模拟实时数据推送
void data_producer(PineVM& vm) {
    std::cout << "[Producer] Thread started." << std::endl;
    
    while (!shutdown_flag) {
        // 1. 模拟数据到达的间隔
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        {
            // 2. 锁定互斥锁以安全地修改共享数据
            std::lock_guard<std::mutex> lock(data_mutex);
            
            // 3. 生成并设置新的K线数据
            bars_produced = load_and_set_data_point(vm);
            
            std::cout << "[Producer] Pushed bar #" << bars_produced << std::endl;
        } // 互斥锁在此处自动释放

        // 4. 通知等待的消费者线程
        cv.notify_one();
    }
    
    std::cout << "[Producer] Thread shutting down." << std::endl;
}

int main(int argc, char* argv[]) {
    
    std::string filename;
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-f" && i + 1 < argc) {
                filename = argv[++i];
            }
        }
    }

    std::string source_code;
    std::string selected_language_from_file = "h"; // Default to Hithink if reading from file

    if (!filename.empty()) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Error: Could not open file " << filename << std::endl;
            return 1;
        }

        // Try to infer language from file extension
        size_t dot_pos = filename.rfind('.');
        if (dot_pos != std::string::npos) {
            std::string ext = filename.substr(dot_pos + 1);
            if (ext == "pine") {
                selected_language_from_file = "p";
            } else if (ext == "el") {
                selected_language_from_file = "e";
            } else if (ext == "hithink" || ext == "tdx") { // Assuming .tdx for Hithink
                selected_language_from_file = "h";
            }
        }
        std::cout << "Compiling from file: " << filename << " (inferred language: " << selected_language_from_file << ")" << std::endl;
        
        std::string language_source;

        std::stringstream buffer;
        buffer << file.rdbuf();
        // 按行分割source_code
        std::string line;
        int all = 0, ok = 0, fail = 0;
        while (std::getline(buffer, line)) {
            language_source = line;
            all++;
            Bytecode bytecode;
        
            try {
                if (selected_language_from_file == "p" || selected_language_from_file == "pine") {
                    std::cout << language_source << std::endl;
                    PineCompiler compiler;
                    bytecode = compiler.compile(language_source);
                } else if (selected_language_from_file == "h" || selected_language_from_file == "hithink") {
                    std::cout << language_source << std::endl;
                    HithinkCompiler compiler;
                    bytecode = compiler.compile(language_source);
                    if (compiler.hadError()) {
                        throw std::runtime_error("Hithink compilation failed.");
                    }
                } else if (selected_language_from_file == "e" || selected_language_from_file == "easylanguage") {
                    std::cout << language_source << std::endl;
                    EasyLanguageParser parser(language_source);
                    std::vector<std::unique_ptr<ELStatement>> el_ast = parser.parse();
                    EasyLanguageCompiler el_compiler;
                    bytecode = el_compiler.compile(el_ast);
                } else {
                    throw std::runtime_error("Invalid language selected. Please choose 'pine', 'easylanguage', or 'hithink'.");
                }


            } catch (const std::exception& e) {
                std::cerr << "Error: " << e.what() << std::endl;
                fail++;
                continue;
            }
            ok ++;
        }

        std::cout << "all:" << all << " ok:" << ok << " fail:" << fail << std::endl;
        
        return 0;
    }

    std::string pine_source = R"(
        ma_length = input.int(14, "MA Length")
        ma = (ta.sma(close, ma_length) + close) / 2;
        rsi = ta.rsi(close, 14)
        plot(rsi, color.green)
        plot(ma, color.red)
    )";

    // Simplified EasyLanguage example for demonstration.
    // Note: Actual EasyLanguage RSI calculation is more complex and often uses built-in functions.
    // This example maps EL's Average and RSI to PineVM's ta.sma and ta.rsi.
    std::string easylanguage_source = R"(
        Inputs: Length(14);
        Variables: MySMA(0), MyRSI(0);

        MySMA = Average(Close, Length);
        MyRSI = RSI(Close, Length); // This will map to ta.rsi(close, Length)

        Plot1(MySMA, "My SMA");
        Plot2(MyRSI, "My RSI");
    )";

    std::string hithink_source = R"(
        TE:LAST(OPEN, 6, 5);
)";
        /*
        MA5:MA(CLOSE,5);
        MA10:MA(CLOSE,10);
        SIG:CROSS(MA5,MA10);
        */
    /*
         //Zero : 0;
        DIF : EMA(CLOSE,6) - EMA(CLOSE,13);
        DEA : EMA(DIF,4);
        macd : 2*(DIF-DEA);
        //主力线:EMA(DIF-MA(REF(DIF,1),1),1)*1.862,colorwhite,LINETHICK1;
        //STICKLINE(MACD>0 AND MACD>=REF(MACD,1),0,MACD,5,0),color0000ff;
        //STICKLINE(MACD>0 AND MACD<REF(MACD,1),0,MACD,5,0),colorffff00;
        //select CROSS(DIF, DEA);
   */
    /*
         Zero : 0;
        DIF : EMA(CLOSE,6) - EMA(CLOSE,13);
        DEA : EMA(DIF,4);
        macd : 2*(DIF-DEA),COLORFF00FF;
        //主力线:EMA(DIF-MA(REF(DIF,1),1),1)*1.862,colorwhite,LINETHICK1;
        //STICKLINE(MACD>0 AND MACD>=REF(MACD,1),0,MACD,5,0),color0000ff;
        //STICKLINE(MACD>0 AND MACD<REF(MACD,1),0,MACD,5,0),colorffff00;
   */
    /*
        RSV:=(CLOSE-LLV(LOW,9))/(HHV(HIGH,9)-LLV(LOW,9))*100;
        K:SMA(RSV,3,1);
        D:SMA(K,3,1);
        J:3*K-2*D;
   */
   /*
        { Hithink/TDX 示例} 
        MA5: MA(CLOSE, 5);
        MA10: MA(C, 10);
        V1 := C > O;
        DRAWTEXT(V1, L, 'Price Up');
    */
    std::string selected_language;
    // 提示用户输入语言类型，并提供默认值
    std::cout << "Enter language to compile (p: pine / e: easylanguage / h: hithink) (default: h): "; 
    std::getline(std::cin, selected_language); // 读取用户输入
    if (selected_language.empty()) {
        selected_language = "h"; // Default to Hithink if no input
        // std::cout << " (default: h): " << selected_language << std::endl; // This line is redundant now
    }
 
    std::cout << "--- Compiling Source ---" << std::endl;
    try {
        std::string bytecode_str;
        if (selected_language == "p" || selected_language == "pine") {
            std::cout << pine_source << std::endl;
            PineCompiler compiler;
            bytecode_str = compiler.compile_to_str(pine_source);
        } else if (selected_language == "h" || selected_language == "hithink") {
            std::cout << hithink_source << std::endl;
            HithinkCompiler compiler;
            bytecode_str = compiler.compile_to_str(hithink_source);
            if (compiler.hadError()) {
                throw std::runtime_error("Hithink compilation failed.");
            }
        } else if (selected_language == "e" || selected_language == "easylanguage") {
            std::cout << easylanguage_source << std::endl;
            EasyLanguageParser parser(easylanguage_source);
            std::vector<std::unique_ptr<ELStatement>> el_ast = parser.parse();
            EasyLanguageCompiler el_compiler;
            Bytecode bytecode = el_compiler.compile(el_ast);
            bytecode_str = bytecodeToTxt(bytecode);
        } else {
            throw std::runtime_error("Invalid language selected. Please choose 'pine', 'easylanguage', or 'hithink'.");
        }
        std::cout << bytecode_str;

        // --- 准备数据源 ---
        std::unique_ptr<DataSource> dataSource;
        std::string ds_type; // Declare ds_type here
        std::cout << "Enter data source type (m: mock / c: csv / j: json) (default: j): ";
        std::string default_ds_type = "j"; // 默认使用JSON
        std::string user_input_ds_type;
        std::getline(std::cin, user_input_ds_type); // Read the actual input for data source selection
        if (user_input_ds_type.empty()) {
            ds_type = default_ds_type; // 如果用户输入为空，则使用默认值
        } else {
            ds_type = user_input_ds_type;
        }

        if (ds_type == "m") {
            dataSource = std::make_unique<MockDataSource>(1000);
        } else if (ds_type == "c") {
            std::string csv_path;
            std::cout << "Enter CSV file path: ";
            std::cin >> csv_path;
            dataSource = std::make_unique<CSVDataSource>(csv_path);
        } else if (ds_type == "j") {
            std::string json_path;
            std::cout << "Enter JSON file path (default: ../db/aapl.json): "; // Prompt for JSON path
            std::string user_json_path;
            std::getline(std::cin, user_json_path); // Read JSON path input
            if (user_json_path.empty()) {
                json_path = "../db/aapl.json"; // Default value
            } else {
                json_path = user_json_path;
            }
            // 注意: 为了测试，可以使用项目中的 db/amzn.json 或 db/aapl.json 文件。
            // 这些文件是换行符分隔的JSON (NDJSON)，每行一个JSON对象。
            // JsonDataSource 被配置为读取具有以下数字键的格式：
            // "7": open, "8": high, "9": low, "11": close, "13": volume
            // 示例行:
            // {"code":"AMZN","trade_date":19970731,"7":29.25,"8":29.25,"9":28.0,"11":28.75,"13":121200}
            dataSource = std::make_unique<JsonDataSource>(json_path);
        } else {
            std::cerr << "Invalid data source type." << std::endl;
            return 1;
        }

        // --- 初始化 VM 并注册数据 ---
        PineVM vm;
        dataSource->loadData(vm);

        // --- 3. 初始化并测量 VM 执行时间 ---
        auto start_time = std::chrono::high_resolution_clock::now();

        std::cout << "\n--- Executing VM ---" << dataSource->getNumBars() << " bars ---" << std::endl;
        {
            std::lock_guard<std::mutex> lock(data_mutex); // 保护初始数据加载
            vm.loadBytecode(bytecode_str);
        }
        int result = vm.execute(dataSource->getNumBars());

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        std::cout << "\n--- Execution Time ---" << std::endl;
        std::cout << "VM execution took: " << duration.count() << " milliseconds" << std::endl;

        if(result != 0) {
            std::cerr << "VM execution failed: " << vm.getLastErrorMessage() << std::endl;
            return 1;
        }
        // 打印计算和绘制的结果
        vm.printPlottedResults();

        std::string output_csv_path;
        std::cout << "Enter output CSV file path (default: ./result.csv): ";
        std::string user_output_csv_path;
        std::getline(std::cin, user_output_csv_path);
        if (user_output_csv_path.empty()) {
            output_csv_path = "./result.csv";
        } else {
            output_csv_path = user_output_csv_path;
        }
        vm.writePlottedResultsToFile(output_csv_path);

        std::string push_csv_path;
        std::cout << "Enter push CSV file path : ";
        std::getline(std::cin, push_csv_path);
        if (!push_csv_path.empty()) {
            // === 启动生产者线程，进入增量计算模式 ===
            std::cout << "\n\n--- [Main] Starting real-time simulation ---" << std::endl;
            std::thread producer_thread(data_producer, std::ref(vm));

            // === 4. 消费者循环（在主线程中） ===
            std::cout << "[Main/Consumer] Waiting for new bars. Press Enter to stop simulation.\n" << std::endl;
            std::thread input_thread([]{
                std::cin.get(); // 等待用户输入
                shutdown_flag = true; // 设置关闭标志
                cv.notify_all(); // 唤醒可能在等待的消费者线程以使其能检查关闭标志
            });
            input_thread.detach(); // 分离输入线程，让它在后台运行

            while(!shutdown_flag) {
                std::unique_lock<std::mutex> lock(data_mutex);
                
                // 等待条件：直到有新的bar被生产出来 或 收到关闭信号
                cv.wait(lock, [&]{ 
                    return bars_produced > vm.getCurrentBarIndex() || shutdown_flag; 
                });

                // 如果被唤醒是因为要关闭，则退出循环
                if (shutdown_flag) {
                    break;
                }

                // 记录需要处理到哪个bar
                int target_bars = bars_produced;
                
                // 提前释放锁，让生产者可以继续工作，而VM在进行计算
                lock.unlock(); 

                // 执行增量计算
                std::cout << "[Main/Consumer] Woke up. Executing up to bar #" << target_bars - 1 << "..." << std::endl;
                vm.execute(target_bars);

                // (可选) 打印最新的结果
                // vm.printPlottedResults(); 
                // 打印最后一个值来观察变化
                const auto& results = vm.getPlottedSeries();
                if (!results.empty()) {
                    const auto& data = results[0].series->data;
                    if (!data.empty()) {
                        std::cout << "[Main/Consumer] Latest value: " << data.back() << std::endl << std::endl;
                    }
                }
            }
            
            // === 清理和收尾 ===
            std::cout << "\n--- [Main] Shutting down simulation ---" << std::endl;
            
            // 等待生产者线程结束
            if (producer_thread.joinable()) {
                producer_thread.join();
            }
            
            std::cout << "\n\n--- [Main] Final Results ---" << std::endl;
            vm.printPlottedResults();
            vm.writePlottedResultsToFile("final_threaded_results.csv");
       }
 
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}