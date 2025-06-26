#pragma once

#include <vector>
#include <string>
#include <variant>
#include <map>
#include <memory>
#include <functional>
#include <stdexcept>
#include <cmath> // for std::isnan, NAN

// 前向声明 PineVM 类，因为内置函数签名需要它
class PineVM;

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
    
    // 变量操作
    LOAD_BUILTIN_VAR,   // 加载一个内置变量 (如 'close')
    LOAD_GLOBAL,        // 加载一个全局变量
    STORE_GLOBAL,       // 存储一个全局变量
    
    JUMP_IF_FALSE,      // 如果栈顶值为假，则跳转
    JUMP,               // 无条件跳转
    // 函数调用
    CALL_BUILTIN_FUNC,  // 调用一个内置函数 (如 'ta.sma')
    CALL_PLOT,          // 调用特殊的 'plot' 函数
    
    // 控制
    HALT                // 停止当前K线柱的执行
};

/**
 * @struct Series
 * @brief 代表一个时间序列数据。PineScript 的核心数据类型。
 */
struct Series {
    std::string name;
    std::vector<double> data;

    /**
     * @brief 获取指定K线柱索引处的值。
     * @param bar_index K线柱的索引。
     * @return double K线柱上的值。如果越界，返回 NaN。
     */
    double getCurrent(int bar_index) const {
        if (bar_index >= 0 && bar_index < data.size()) {
            return data[bar_index];
        }
        return NAN; 
    }

    /**
     * @brief 设置指定K线柱索引处的值。如果需要，会自动扩展向量。
     * @param bar_index K线柱的索引。
     * @param value 要设置的值。
     */
    void setCurrent(int bar_index, double value) {
        if (bar_index >= data.size()) {
            // 用 NaN 填充直到目标索引
            data.resize(bar_index + 1, NAN);
        }
        data[bar_index] = value;
    }
};

/**
 * @using Value
 * @brief 一个通用的值类型，可以持有VM中所有可能的数据类型。
 */
using Value = std::variant<
    double,                           // 用于 int 和 float
    bool,                             // 用于布尔值
    std::string,                      // 用于字符串
    std::shared_ptr<Series>           // 用于序列对象 (使用智能指针管理生命周期)
>;

/**
 * @struct Instruction
 * @brief 代表一条单独的虚拟机指令。
 */
struct Instruction {
    OpCode op;
    int operand = 0; // 操作数, 通常是常量池的索引或全局变量的槽位
};

/**
 * @struct Bytecode
 * @brief 代表一个已编译的脚本，包含指令序列和常量池。
 */
struct Bytecode {
    std::vector<Instruction> instructions;
    std::vector<Value> constant_pool;
};


//-----------------------------------------------------------------------------
// 2. PineVM 类 (The Virtual Machine Class)
//-----------------------------------------------------------------------------

/**
 * @class PineVM
 * @brief 一个用于执行 PineScript 字节码的堆栈式虚拟机。
 */
class PineVM {
public:
    /**
     * @brief PineVM 的构造函数。
     * @param total_bars 要模拟的历史K线柱总数。
     */
    PineVM(int total_bars);

    /**
     * @brief 加载要执行的字节码。
     * @param code 指向 Bytecode 对象的指针。VM 不会获得其所有权。
     */
    void loadBytecode(const Bytecode* code);

    /**
     * @brief 加载市场数据作为内置序列。
     * @param name 序列的名称 (例如 "close", "high")。
     * @param data 包含市场数据的向量。
     */
    void loadMarketData(const std::string& name, std::vector<double> data);
    
    /**
     * @brief 执行已加载的字节码，遍历所有历史K线柱。
     */
    void execute();

    // --- 公共API，主要供内置函数回调使用 ---
    
    /**
     * @brief 从操作数栈中弹出一个值。
     * @return Value 栈顶的值。
     * @throws std::runtime_error 如果栈为空。
     */
    Value pop();

    /**
     * @brief 将一个值压入操作数栈。
     * @param val 要压入的值。
     */
    void push(Value val);
    
    /**
     * @brief 获取当前正在执行的K线柱索引。
     * @return int 当前的 bar_index。
     */
    int getCurrentBarIndex() const { return bar_index; }

private:
    /**
     * @using BuiltinFunction
     * @brief 定义了内置C++函数的函数签名。
     * @return Value 函数的计算结果。
     */
    using BuiltinFunction = std::function<Value(PineVM&)>;

    // --- 内部状态 ---
    const Bytecode* bytecode = nullptr;
    const Instruction* ip = nullptr; // 指令指针
    std::vector<Value> stack;        // 操作数栈,
    std::vector<Value> globals;      // 全局变量存储槽

    // --- 执行上下文 ---
    int total_bars;
    int bar_index;

    // --- 内置环境 ---
    std::map<std::string, Value> built_in_vars;
    std::map<std::string, BuiltinFunction> built_in_funcs;
    std::map<std::string, std::shared_ptr<Series>> builtin_func_cache;

    // --- 私有辅助函数 ---

    /**
     * @brief 执行当前K线柱(bar_index)的字节码。
     */
    void runCurrentBar();
    
    double getNumericValue(const Value& val) const;
    /**
     * @brief 注册所有由C++实现的内置函数 (如 ta.sma, input.int)。
     */
    void registerBuiltins();
};