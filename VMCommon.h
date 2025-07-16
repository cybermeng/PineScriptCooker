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
    
    // 变量操作
    LOAD_BUILTIN_VAR,   // 加载一个内置变量 (如 'close')
    LOAD_GLOBAL,        // 加载一个全局变量
    STORE_GLOBAL,       // 存储一个全局变量
    RENAME_SERIES,      // 重命名栈顶的序列
    STORE_AND_PLOT_GLOBAL, // 存储到全局变量并添加到绘图列表
    
    JUMP_IF_FALSE,      // 如果栈顶值为假，则跳转
    JUMP,               // 无条件跳转
    // 函数调用
    CALL_BUILTIN_FUNC,  // 调用一个内置函数 (如 'ta.sma')
    CALL_PLOT,          // 调用特殊的 'plot' 函数 (plot_name, series, color)
    
    // 控制
    HALT                // 停止当前K线柱的执行
};

/**
 * @struct Series
 * @brief 代表一个时间序列数据。PineScript 的核心数据类型。
 */
struct Series : public std::enable_shared_from_this<Series> {
    std::string name;
    std::vector<double> data;
    double getCurrent(int bar_index); // 修改：变为非 const，实现移至 .cpp

    /**
     * @brief 设置指定K线柱索引处的值。如果需要，会自动扩展向量。
     * 主要用于计算指标（如SMA）的结果，而不是市场数据。
     * @param bar_index K线柱的索引。
     * @param value 要设置的值。
     */
    void setCurrent(int bar_index, double value); // 修改：实现移至 .cpp

    void setName(const std::string& name);
};

/**
 * @using Value
 * @brief 一个通用的值类型，可以持有VM中所有可能的数据类型。
 */
using Value = std::variant<
    std::monostate, 
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
    std::vector<std::string> global_name_pool;
    int varNum = 0;     //中间变量个数
};

/**
 * @struct PlottedSeries
 * @brief 代表一个已绘制的序列，包含其数据和可视化属性。
 */
struct PlottedSeries {
    std::shared_ptr<Series> series;
    std::string color;
};
