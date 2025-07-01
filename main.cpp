#include "PineVM.h"
#include "PineScript/PineCompiler.h" // 新增：PineScript 编译器头文件
#include "DataSource.h" // 新增：数据源层
#include "DataSource/CSVDataSource.h" // 新增：CSV数据源
#include <chrono> // 新增：用于时间测量
#include <iostream>
#include <vector>
#include <variant>
#include <iomanip>
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
            case OpCode::STORE_GLOBAL: {
                std::cout << std::left << std::setw(20) << opName 
                          << std::right << std::setw(4) << instruction.operand << " ";
                // For these, operand is an index to constant pool (for name) or global slot
                // We can print the constant value if it's a name.
                if (instruction.op == OpCode::LOAD_BUILTIN_VAR || instruction.op == OpCode::CALL_BUILTIN_FUNC) {
                     printValue(bytecode.constant_pool[instruction.operand]);
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
                std::cout << opName << std::endl;
                offset++;
                break;
        }
    }
}
int main() {
    std::string pine_source = R"(
        ma_length = input.int(14, "MA Length")
        ma = ta.sma(close, ma_length)
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
        { Hithink/TDX 示例 }
        MA5: MA(CLOSE, 5);
        MA10: MA(C, 10);
        V1 := C > O;
        DRAWTEXT(V1, L, 'Price Up');
    )";

    std::string selected_language;
    std::cout << "Enter language to compile (p: pine / e: easylanguage / h: hithink): ";
    std::cin >> selected_language;

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

        // --- 准备数据源 ---
        std::unique_ptr<DataSource> dataSource;
        std::string ds_type;
        std::cout << "\nEnter data source type (m: mock / c: csv): ";
        std::cin >> ds_type;

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
        vm.loadBytecode(&bytecode);
        vm.execute();

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        std::cout << "\n--- Execution Time ---" << std::endl;
        std::cout << "VM execution took: " << duration.count() << " milliseconds" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}