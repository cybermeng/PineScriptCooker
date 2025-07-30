#pragma once

#include <vector>
#include <string>
#include <variant>
#include <map>
#include <memory>
#include <functional>
#include <stdexcept>
#include <cmath> // for std::isnan, NAN

//-----------------------------------------------------------------------------
// 1. 数据结构 (Data Structures)
//-----------------------------------------------------------------------------

/**
 * @enum OpCode
 * @brief 定义了 PineVM 的所有操作码。
 */
enum class OpCode {
    PUSH_CONST,         // 将常量池中的一个值压入栈顶
    POP,                // 弹出栈顶元素
    
    // 算术与逻辑运算 (简化版)
    ADD,
    SUB,
    MUL,
    DIV,
    LESS,
    LESS_EQUAL,
    EQUAL_EQUAL,
    BANG_EQUAL,
    GREATER,
    GREATER_EQUAL,
    SUBSCRIPT,          // 新增：下标访问操作

    LOGICAL_AND,
    LOGICAL_OR,
    
    // 变量操作
    LOAD_BUILTIN_VAR,   // 加载一个内置变量 (如 'close')
    LOAD_GLOBAL,        // 加载一个全局变量
    STORE_GLOBAL,       // 存储一个全局变量
    STORE_EXPORT,       // 存储一个输出值
    RENAME_SERIES,      // 重命名栈顶的序列
    
    JUMP_IF_FALSE,      // 如果栈顶值为假，则跳转
    JUMP,               // 无条件跳转
    // 函数调用
    CALL_BUILTIN_FUNC,  // 调用一个内置函数 (如 'ta.sma')
    
    // 控制
    HALT                // 停止当前K线柱的执行
};

// ... 其余部分与原文件相同 ...
struct Series : public std::enable_shared_from_this<Series> {
    std::string name;
    std::vector<double> data;
    double getCurrent(int bar_index); 
    void setCurrent(int bar_index, double value);
    void setName(const std::string& name);
};

using Value = std::variant<
    std::monostate, 
    double,                           
    bool,                             
    std::string,                      
    std::shared_ptr<Series>           
>;

struct Instruction {
    OpCode op;
    int operand = 0; 
};

struct Bytecode {
    std::vector<Instruction> instructions;
    std::vector<Value> constant_pool;
    std::vector<std::string> global_name_pool;
    int varNum = 0;
};

struct ExportedSeries {
    std::string name;
    std::string color;
};

std::string bytecodeToTxt(const Bytecode& bytecode);
Bytecode txtToBytecode(const std::string& txt);