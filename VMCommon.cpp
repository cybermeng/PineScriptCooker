#include "VMCommon.h"
#include <sstream>
#include <iomanip> // For std::fixed and std::setprecision
#include <map>     // For opCodeMap in txtToBytecode
#include <optional> // For std::optional in txtToBytecode
#include <cstdint> // For uint32_t
#include <iostream> // For debug output

typedef uint32_t _checksum_t;

/**
 * @brief (内部辅助类) 实现32位 FNV-1a 哈希算法，支持流式更新。
 * FNV-1a 是一种简单且高效的非加密哈希算法，其结果在所有平台上都是确定性的。
 */
class FNV1aHash {
public:
    /**
     * @brief 构造函数，用32位FNV偏移基准值初始化哈希。
     */
    FNV1aHash() : hash_(FNV_OFFSET_BASIS) {}

    /**
     * @brief 用一块内存数据更新哈希值。
     * @param data 指向数据的指针。
     * @param size 数据的字节大小。
     */
    void update(const char* data, size_t size) {
        for (size_t i = 0; i < size; ++i) {
            // 核心算法：先异或，再乘质数
            hash_ ^= static_cast<uint32_t>(data[i]);
            hash_ *= FNV_PRIME;
        }
    }

    /**
     * @brief 用一个字符串更新哈希值。
     * @param str 输入的字符串。
     */
    void update(const std::string& str) {
        update(str.data(), str.size());
    }

    /**
     * @brief 获取最终的32位哈希值。
     * @return 32位哈希结果。
     */
    _checksum_t finalize() const {
        return hash_;
    }

private:
    // FNV-1a 32位常量
    static constexpr _checksum_t FNV_PRIME = 0x01000193;        // 16777619
    static constexpr _checksum_t FNV_OFFSET_BASIS = 0x811c9dc5; // 2166136261
    
    _checksum_t hash_;
};

/**
 * @brief (内部辅助函数) 为 Bytecode 对象生成一个确定性的校验和。
 * 
 * 这个函数通过将所有指令、常量和全局名称序列化为一个规范的字符串，
 * 然后对该字符串应用 std::hash 来工作。
 * 为了确保一致性，两个函数 bytecodeToTxt 和 txtToBytecode 必须使用这个函数。
 * 
 * @param bytecode 要计算校验和的 Bytecode 对象。
 * @return 代表校验和的  值。
 */
_checksum_t _generateChecksum(const Bytecode& bytecode) {
    std::stringstream canonical_stream;

    // 0. 序列化变量数量 (新增)
    canonical_stream << bytecode.varNum << "|";

    // 1. 序列化指令
    for (const auto& instr : bytecode.instructions) {
        // 将 OpCode 转换为整数以获得稳定表示
        canonical_stream << static_cast<int>(instr.op) << ":" << instr.operand << ";";
    }
    canonical_stream << "|"; // 分隔符

    // 2. 序列化常量池
    for (const auto& constant : bytecode.constant_pool) {
        std::visit([&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                canonical_stream << "m;";
            } else if constexpr (std::is_same_v<T, double>) {
                canonical_stream << "d:" << arg << ";";
            } else if constexpr (std::is_same_v<T, bool>) {
                canonical_stream << "b:" << (arg ? '1' : '0') << ";";
            } else if constexpr (std::is_same_v<T, std::string>) {
                canonical_stream << "s:" << arg.length() << ":" << arg << ";";
            } else if constexpr (std::is_same_v<T, std::shared_ptr<Series>>) {
                canonical_stream << "r:" << arg->name.length() << ":" << arg->name << ";";
            }
        }, constant);
    }
    canonical_stream << "|"; // 分隔符

    // 3. 序列化全局名称池
    for (const auto& name : bytecode.global_name_pool) {
        canonical_stream << name << ";";
    }

    // 4. 计算哈希值
    FNV1aHash hasher;
    std::string canonical_string = canonical_stream.str();
    //std::cout << canonical_string << std::endl;
    hasher.update(canonical_string);
    return hasher.finalize();
}

std::string bytecodeToTxt(const Bytecode& bytecode)
{
    std::string result = "--- Bytecode ---\n";
    for (int i = 0; i < bytecode.instructions.size(); ++i) {
        const auto& instr = bytecode.instructions[i];
        result += std::to_string(i) + ": ";
        switch (instr.op) {
            case OpCode::PUSH_CONST:
                result += "PUSH_CONST " + std::to_string(instr.operand);
                // Optionally, print the constant value itself
                // result += " (" + std::visit([](auto&& arg){ return std::to_string(arg); }, bytecode.constant_pool[instr.operand]) + ")";
                break;
            case OpCode::POP:
                result += "POP";
                break;
            case OpCode::ADD: result += "ADD " + std::to_string(instr.operand); break;
            case OpCode::SUB: result += "SUB " + std::to_string(instr.operand); break;
            case OpCode::MUL: result += "MUL " + std::to_string(instr.operand); break;
            case OpCode::DIV: result += "DIV " + std::to_string(instr.operand); break;
            case OpCode::LESS: result += "LESS " + std::to_string(instr.operand); break;
            case OpCode::LESS_EQUAL: result += "LESS_EQUAL " + std::to_string(instr.operand); break;
            case OpCode::EQUAL_EQUAL: result += "EQUAL_EQUAL " + std::to_string(instr.operand); break;
            case OpCode::BANG_EQUAL: result += "BANG_EQUAL " + std::to_string(instr.operand); break;
            case OpCode::GREATER: result += "GREATER " + std::to_string(instr.operand); break;
            case OpCode::GREATER_EQUAL: result += "GREATER_EQUAL " + std::to_string(instr.operand); break;
            case OpCode::LOGICAL_AND: result += "LOGICAL_AND " + std::to_string(instr.operand); break;
            case OpCode::LOGICAL_OR: result += "LOGICAL_OR " + std::to_string(instr.operand); break;
            case OpCode::LOAD_BUILTIN_VAR:
                result += "LOAD_BUILTIN_VAR " + std::to_string(instr.operand);
                break;
            case OpCode::LOAD_GLOBAL:
                result += "LOAD_GLOBAL " + std::to_string(instr.operand);
                break;
            case OpCode::STORE_GLOBAL:
                result += "STORE_GLOBAL " + std::to_string(instr.operand);
                break;
            case OpCode::RENAME_SERIES:
                result += "RENAME_SERIES";
                break;
            case OpCode::STORE_AND_PLOT_GLOBAL:
                result += "STORE_AND_PLOT_GLOBAL " + std::to_string(instr.operand);
                break;
            case OpCode::JUMP_IF_FALSE:
                result += "JUMP_IF_FALSE " + std::to_string(instr.operand);
                break;
            case OpCode::JUMP:
                result += "JUMP " + std::to_string(instr.operand);
                break;
            case OpCode::CALL_BUILTIN_FUNC:
                result += "CALL_BUILTIN_FUNC " + std::to_string(instr.operand);
                break;
            case OpCode::CALL_PLOT:
                result += "CALL_PLOT " + std::to_string(instr.operand);
                break;
            case OpCode::HALT:
                result += "HALT";
                break;
            default:
                result += "UNKNOWN_OPCODE";
                break;
        }
        result += "\n";
    }

    result += "\n--- Variable Number ---\n";
    result += std::to_string(bytecode.varNum);
    result += "\n";

    result += "\n--- Constant Pool ---\n";
    for (int i = 0; i < bytecode.constant_pool.size(); ++i) {
        result += std::to_string(i) + ": ";
        std::visit([&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, double>) {
                result += std::to_string(arg);
            } else if constexpr (std::is_same_v<T, bool>) {
                result += (arg ? "true" : "false");
            } else if constexpr (std::is_same_v<T, std::string>) {
                result += "\"" + arg + "\"";
            } else if constexpr (std::is_same_v<T, std::shared_ptr<Series>>) {
                result += "Series(" + arg->name + ")";
            } else if constexpr (std::is_same_v<T, std::monostate>) {
                result += "monostate";
            }
        }, bytecode.constant_pool[i]);
        result += "\n";
    }

    result += "\n--- Global Name Pool ---\n";
    for (int i = 0; i < bytecode.global_name_pool.size(); ++i) {
        result += std::to_string(i) + ": " + bytecode.global_name_pool[i] + "\n";
    }

    result += "\n--- Validation ---\n";
    result += "Checksum: " + std::to_string(_generateChecksum(bytecode)) + "\n";
    return result;
}

Bytecode txtToBytecode(const std::string& txt)
{
    Bytecode bytecode;
    std::stringstream ss(txt);
    std::string line;

    // 用于跟踪当前正在解析哪个部分
    enum class ParsingSection {
        NONE,
        INSTRUCTIONS,
        VARIABLE_NUMBER, // 新增
        CONSTANTS,
        GLOBALS,
        VALIDATION
    };
    ParsingSection currentSection = ParsingSection::NONE;

    // 创建从字符串到 OpCode 的映射，以便于查找
    const std::map<std::string, OpCode> opCodeMap = {
        {"PUSH_CONST", OpCode::PUSH_CONST},
        {"POP", OpCode::POP},
        {"ADD", OpCode::ADD},
        {"SUB", OpCode::SUB},
        {"MUL", OpCode::MUL},
        {"DIV", OpCode::DIV},
        {"LESS", OpCode::LESS},
        {"LESS_EQUAL", OpCode::LESS_EQUAL},
        {"EQUAL_EQUAL", OpCode::EQUAL_EQUAL},
        {"BANG_EQUAL", OpCode::BANG_EQUAL},
        {"GREATER", OpCode::GREATER},
        {"GREATER_EQUAL", OpCode::GREATER_EQUAL},
        {"LOGICAL_AND", OpCode::LOGICAL_AND},
        {"LOGICAL_OR", OpCode::LOGICAL_OR},
        {"LOAD_BUILTIN_VAR", OpCode::LOAD_BUILTIN_VAR},
        {"LOAD_GLOBAL", OpCode::LOAD_GLOBAL},
        {"STORE_GLOBAL", OpCode::STORE_GLOBAL},
        {"RENAME_SERIES", OpCode::RENAME_SERIES},
        {"STORE_AND_PLOT_GLOBAL", OpCode::STORE_AND_PLOT_GLOBAL},
        {"JUMP_IF_FALSE", OpCode::JUMP_IF_FALSE},
        {"JUMP", OpCode::JUMP},
        {"CALL_BUILTIN_FUNC", OpCode::CALL_BUILTIN_FUNC},
        {"CALL_PLOT", OpCode::CALL_PLOT},
        {"HALT", OpCode::HALT}
    };

    std::optional<_checksum_t> expected_checksum;

    while (std::getline(ss, line)) {
        // 跳过空行
        if (line.empty()) {
            continue;
        }

        // 切换解析部分
        if (line == "--- Bytecode ---") {
            currentSection = ParsingSection::INSTRUCTIONS;
            continue;
        }
        if (line == "--- Variable Number ---") { // 新增
            currentSection = ParsingSection::VARIABLE_NUMBER;
            continue;
        }
        if (line == "--- Constant Pool ---") {
            currentSection = ParsingSection::CONSTANTS;
            continue;
        }
        if (line == "--- Global Name Pool ---") {
            currentSection = ParsingSection::GLOBALS;
            continue;
        }
         if (line == "--- Validation ---") { 
            currentSection = ParsingSection::VALIDATION;
            continue;
        }

        // 根据当前部分解析行
        switch (currentSection) {
            case ParsingSection::INSTRUCTIONS: {
                std::stringstream line_ss(line);
                std::string index_part, op_str;
                
                line_ss >> index_part >> op_str;
                
                auto it = opCodeMap.find(op_str);
                if (it == opCodeMap.end()) {
                    throw std::runtime_error("Unknown opcode in bytecode text: " + op_str);
                }
                
                Instruction instr;
                instr.op = it->second;
                
                line_ss >> instr.operand; 
                
                bytecode.instructions.push_back(instr);
                break;
            }

            case ParsingSection::VARIABLE_NUMBER: { // 新增解析逻辑
                try {
                    bytecode.varNum = std::stoi(line);
                } catch (const std::exception& e) {
                    throw std::runtime_error("Could not parse variable number: " + line);
                }
                break;
            }

            case ParsingSection::CONSTANTS: {
                size_t colon_pos = line.find(": ");
                if (colon_pos == std::string::npos) continue; 
                
                std::string valueStr = line.substr(colon_pos + 2);
                Value val;

                if (valueStr == "true") {
                    val = true;
                } else if (valueStr == "false") {
                    val = false;
                } else if (valueStr.front() == '"' && valueStr.back() == '"') {
                    val = valueStr.substr(1, valueStr.length() - 2);
                } else if (valueStr.rfind("Series(", 0) == 0 && valueStr.back() == ')') {
                    std::string series_name = valueStr.substr(7, valueStr.length() - 8);
                    auto series = std::make_shared<Series>();
                    series->setName(series_name);
                    val = series;
                } else if (valueStr == "monostate") {
                    val = std::monostate{};
                }
                else {
                    try {
                        val = std::stod(valueStr);
                    } catch (const std::invalid_argument& e) {
                        throw std::runtime_error("Could not parse constant value: " + valueStr);
                    }
                }
                bytecode.constant_pool.push_back(val);
                break;
            }

            case ParsingSection::GLOBALS: {
                size_t colon_pos = line.find(": ");
                if (colon_pos == std::string::npos) continue;
                
                std::string globalName = line.substr(colon_pos + 2);
                bytecode.global_name_pool.push_back(globalName);
                break;
            }

            case ParsingSection::VALIDATION: {
                std::string checksum_label;
                _checksum_t checksum_value;
                std::stringstream line_ss(line);
                line_ss >> checksum_label >> checksum_value;
                if (checksum_label == "Checksum:") {
                    expected_checksum = checksum_value;
                }
                break;
            }

            case ParsingSection::NONE:
                break;
        }
    }

    // --- 最后的校验步骤 ---
    if (!expected_checksum.has_value()) {
        throw std::runtime_error("Validation checksum not found in the bytecode text.");
    }
    
    _checksum_t actual_checksum = _generateChecksum(bytecode);

    if (actual_checksum != expected_checksum.value()) {
        std::stringstream error_msg;
        error_msg << "Checksum mismatch! The bytecode text is corrupted or has been tampered with.\n"
                  << "Expected: " << expected_checksum.value() << "\n"
                  << "Actual:   " << actual_checksum;
        throw std::runtime_error(error_msg.str());
    }
    
    return bytecode;
}