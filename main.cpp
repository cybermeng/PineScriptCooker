#include "PineVM.h"
#include "duckdb.h" // 新增：包含 DuckDB C API 头文件
#include "PineScript/PineCompiler.h" // 新增：PineScript 编译器头文件
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

        // --- 设置 DuckDB 并加载数据 ---
        // Increase data size significantly
        int num_bars = 1000;
        std::vector<double> close_prices(num_bars);
        for (int i = 0; i < num_bars; ++i) {
            close_prices[i] = 100.0 + (i % 20 - 10) * 0.5;  // Example oscillating data
        }

        // 1. 初始化 VM，这将创建内存数据库
        // 传递空字符串表示内存数据库
        PineVM vm(close_prices.size(), ""); 
        duckdb_connection con = vm.getConnection();
        if (!con) {
            throw std::runtime_error("Failed to get DuckDB connection from PineVM");
        }

        std::cout << "\n--- Preparing Data in DuckDB ---" << std::endl;

        // 创建一个市场数据表
        duckdb_result result;
        const char* create_table_query = "CREATE TABLE market_data(bar_id INTEGER PRIMARY KEY, open DOUBLE, high DOUBLE, low DOUBLE, close DOUBLE)";
        if (duckdb_query(con, create_table_query, &result) == DuckDBError) {
            std::string error_msg = "Failed to create table: ";
            error_msg += duckdb_result_error(&result);
            duckdb_destroy_result(&result);
            throw std::runtime_error(error_msg);
        }
        duckdb_destroy_result(&result);

        // 使用 Appender 高效地插入数据
        duckdb_appender appender;
        if (duckdb_appender_create(con, nullptr, "market_data", &appender) == DuckDBError) {
            throw std::runtime_error("Failed to create appender");
        }
        for (int i = 0; i < close_prices.size(); ++i) {
            duckdb_appender_begin_row(appender);
            duckdb_append_int32(appender, i); // bar_id
            duckdb_append_double(appender, NAN); // open (示例数据)
            duckdb_append_double(appender, NAN); // high (示例数据)
            duckdb_append_double(appender, NAN); // low (示例数据)
            duckdb_append_double(appender, close_prices[i]); // close
            duckdb_appender_end_row(appender);
        }
        duckdb_appender_destroy(&appender);
        std::cout << "Loaded " << close_prices.size() << " bars into DuckDB." << std::endl;

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