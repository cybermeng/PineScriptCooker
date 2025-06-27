#include "Compiler.h"
#include "PineVM.h"
#include "duckdb.h" // 新增：包含 DuckDB C API 头文件
#include <iostream>
#include <vector>
#include <variant>
#include <iomanip>

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
    std::string source = R"(
ma_length = input.int(14, "MA Length")
ma = ta.sma(close, ma_length)
rsi = ta.rsi(close, 14)
plot(rsi, color.green)
plot(ma, color.red)
)";

    std::cout << "--- Compiling Source ---" << std::endl;
    std::cout << source << std::endl;
    
    Compiler compiler;
    try {
        Bytecode bytecode = compiler.compile(source);
        
        disassembleChunk(bytecode, "Compiled Script");

        // --- 设置 DuckDB 并加载数据 ---
        std::vector<double> close_prices = {100, 102, 105, 103, 106, 108, 110, 111, 115, 120};

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

        // 3. 初始化并运行 VM
        std::cout << "\n--- Executing VM ---" << std::endl;
        vm.loadBytecode(&bytecode);
        vm.execute();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}