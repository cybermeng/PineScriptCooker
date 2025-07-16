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
#include "Hithink/HithinkAST.h" // For HithinkStatement definition
#include "Hithink/HithinkCompiler.h"

// --- 调试辅助函数 ---

// 辅助函数：打印 Value
void printValue(const Value& value) {
    std::visit([](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, double>)
            std::cout << arg;
        else if constexpr (std::is_same_v<T, bool>)
            std::cout << (arg ? "true" : "false");
        else if constexpr (std::is_same_v<T, std::string>)
            std::cout << "'" << arg << "'";
        else if constexpr (std::is_same_v<T, std::shared_ptr<Series>>)
            std::cout << "Series(" << arg->name << ")";
    }, value);
}

// 辅助函数：获取操作码名称
const char* getOpCodeName(OpCode op) {
    switch (op) {
        case OpCode::PUSH_CONST:        return "PUSH_CONST";
        case OpCode::POP:               return "POP";
        case OpCode::ADD:               return "ADD";
        case OpCode::SUB:               return "SUB";
        case OpCode::MUL:               return "MUL";
        case OpCode::DIV:               return "DIV";
        case OpCode::LESS:              return "LESS";
        case OpCode::LESS_EQUAL:        return "LESS_EQUAL";
        case OpCode::EQUAL_EQUAL:       return "EQUAL_EQUAL";
        case OpCode::BANG_EQUAL:        return "BANG_EQUAL";
        case OpCode::GREATER:           return "GREATER";
        case OpCode::LOAD_BUILTIN_VAR:  return "LOAD_BUILTIN_VAR";
        case OpCode::LOAD_GLOBAL:       return "LOAD_GLOBAL";
        case OpCode::STORE_GLOBAL:      return "STORE_GLOBAL";
        case OpCode::RENAME_SERIES:     return "RENAME_SERIES";
        case OpCode::STORE_AND_PLOT_GLOBAL: return "STORE_AND_PLOT_GLOBAL";
        case OpCode::JUMP_IF_FALSE:     return "JUMP_IF_FALSE";
        case OpCode::JUMP:              return "JUMP";
        case OpCode::CALL_BUILTIN_FUNC: return "CALL_BUILTIN_FUNC";
        case OpCode::CALL_PLOT:         return "CALL_PLOT";
        case OpCode::HALT:              return "HALT";
        default:                        return "UNKNOWN";
    }
}

// 用于反汇编字节码以进行调试的辅助函数
void disassembleChunk(const Bytecode& bytecode, const std::string& name) {
    std::cout << "--- " << name << " ---" << std::endl;
    for (int offset = 0; offset < bytecode.instructions.size(); ) {
        std::cout << std::setw(4) << std::setfill('0') << offset << " ";
        const auto& instruction = bytecode.instructions[offset];
        const char* opName = getOpCodeName(instruction.op);
        
        switch (instruction.op) {
            case OpCode::PUSH_CONST: {
                std::cout << std::left << std::setw(20) << opName 
                          << std::right << std::setw(4) << instruction.operand << " ";
                printValue(bytecode.constant_pool[instruction.operand]);
                std::cout << std::endl;
                offset++;
                break;
            }
            case OpCode::LOAD_BUILTIN_VAR:
            case OpCode::CALL_BUILTIN_FUNC:
            case OpCode::LOAD_GLOBAL:
            case OpCode::STORE_GLOBAL:
            case OpCode::STORE_AND_PLOT_GLOBAL: {
                std::cout << std::left << std::setw(20) << opName 
                          << std::right << std::setw(4) << instruction.operand << " ";
                // For these, operand is an index to constant pool (for name) or global slot
                // We can print the constant value if it's a name.
                if (instruction.op == OpCode::LOAD_BUILTIN_VAR || instruction.op == OpCode::CALL_BUILTIN_FUNC) {
                     printValue(bytecode.constant_pool[instruction.operand]);
                } else if (instruction.op == OpCode::LOAD_GLOBAL || instruction.op == OpCode::STORE_GLOBAL || instruction.op == OpCode::STORE_AND_PLOT_GLOBAL) {
                     printValue(bytecode.global_name_pool[instruction.operand]);
                }
                std::cout << std::endl;
                offset++;
                break;
            }
            case OpCode::JUMP_IF_FALSE:
            case OpCode::JUMP: {
                std::cout << std::left << std::setw(20) << opName
                          << std::right << std::setw(4) << instruction.operand << " (target)" << std::endl;
                offset++;
                break;
            }
            default:
                std::cout << std::left << std::setw(20) << opName
                          << std::right << std::setw(4) << instruction.operand
                          << std::endl;
                offset++;
                break;
        }
    }
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
            disassembleChunk(bytecode, "Compiled Script");
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
        aa=min(o,c);
        bb=abs(o-c);
        select count(( h-aa>3*bb and 3*(aa-l)<bb and bb>0 and c>ma(c,5) and c>ma(c,10) and c>ma(c,20)),1)>=1
    )";
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
        Bytecode bytecode;
        if (selected_language == "p" || selected_language == "pine") {
            std::cout << pine_source << std::endl;
            PineCompiler compiler;
            bytecode = compiler.compile(pine_source);
        } else if (selected_language == "h" || selected_language == "hithink") {
            std::cout << hithink_source << std::endl;
            HithinkCompiler compiler;
            bytecode = compiler.compile(hithink_source);
            if (compiler.hadError()) {
                throw std::runtime_error("Hithink compilation failed.");
            }
        } else if (selected_language == "e" || selected_language == "easylanguage") {
            std::cout << easylanguage_source << std::endl;
            EasyLanguageParser parser(easylanguage_source);
            std::vector<std::unique_ptr<ELStatement>> el_ast = parser.parse();
            EasyLanguageCompiler el_compiler;
            bytecode = el_compiler.compile(el_ast);
        } else {
            throw std::runtime_error("Invalid language selected. Please choose 'pine', 'easylanguage', or 'hithink'.");
        }

        disassembleChunk(bytecode, "Compiled Script");

        std::string bytecodeString = PineVM::bytecodeToTxt(bytecode);
        std::cout << bytecodeString;

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
            // 注意: 为了测试，请创建一个名为 market_data.csv 的示例文件，包含以下列：
            // open,high,low,close,volume
            // 例如:
            // open,high,low,close,volume
            // 100.1,100.2,99.9,100.0,1000
            // 100.0,100.3,99.8,100.1,1200
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
        PineVM vm(dataSource->getNumBars());
        dataSource->loadData(vm);

        // --- 3. 初始化并测量 VM 执行时间 ---
        auto start_time = std::chrono::high_resolution_clock::now();

        std::cout << "\n--- Executing VM ---" << std::endl;
        vm.loadBytecode(bytecodeString);
        int result = vm.execute();

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        std::cout << "\n--- Execution Time ---" << std::endl;
        std::cout << "VM execution took: " << duration.count() << " milliseconds" << std::endl;

        if(result != 0) {
            std::cerr << "VM execution failed." << std::endl;
            return 1;
        }
        // 打印计算和绘制的结果
        vm.printPlottedResults();

        std::string output_csv_path;
        std::cout << "Enter output CSV file path (default: ../db/result.csv): ";
        std::string user_output_csv_path;
        std::getline(std::cin, user_output_csv_path);
        if (user_output_csv_path.empty()) {
            output_csv_path = "../db/result.csv";
        } else {
            output_csv_path = user_output_csv_path;
        }
        vm.writePlottedResultsToFile(output_csv_path);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}