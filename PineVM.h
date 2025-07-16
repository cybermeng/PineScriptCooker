#pragma once

#include <vector>
#include <string>
#include <variant>
#include <map>
#include <memory>
#include <functional>
#include <stdexcept>
#include <cmath> // for std::isnan, NAN
#include "VMCommon.h"

//-----------------------------------------------------------------------------
// PineVM 类 (The Virtual Machine Class)
//-----------------------------------------------------------------------------

/**
 * @class PineVM
 * @brief 一个用于执行 PineScript 字节码的堆栈式虚拟机。
 */
class PineVM {
public:
    /**
     * @brief PineVM 的构造函数
     * @param total_bars 要模拟的历史K线柱总数
     */
    PineVM(int total_bars);
    ~PineVM();

    /**
     * @brief 加载要执行的字节码。
     * @param 
     */
    void loadBytecode(const std::string& code);
    
    /**
     * @brief 执行已加载的字节码，遍历所有历史K线柱。
     */
    int execute();

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

    void pushNumbericValue(double val, int operand);
    
    /**
     * @brief 获取当前正在执行的K线柱索引。
     * @return int 当前的 bar_index。
     */
    int getCurrentBarIndex() const { return bar_index; }

    /**
     * @brief 获取所有已绘制的序列及其属性。
     * @return const std::vector<PlottedSeries>& 对已绘制序列向量的常量引用。
     */
    const std::vector<PlottedSeries>& getPlottedSeries() const { return plotted_series; }

    /**
     * @brief 打印所有已绘制的序列及其数据。
     */
    void printPlottedResults() const;

      // 新的公共接口：写入到文件
    void writePlottedResultsToFile(const std::string& filename) const;

    // 新的公共接口：获取结果为字符串
    std::string getPlottedResultsAsString() const;

    void registerSeries(const std::string& name, std::shared_ptr<Series> series);

    /**
     * @brief 获取一个已注册的序列。
     * @param name 序列的名称 (例如 "open", "close").
     * @return Series* 指向序列对象的原始指针，如果未找到则返回 nullptr。
     */
    Series* getSeries(const std::string& name) {
        auto it = built_in_vars.find(name);
        if (it != built_in_vars.end()) {
            // 尝试将 Value 转换为 std::shared_ptr<Series>
            if (std::holds_alternative<std::shared_ptr<Series>>(it->second)) {
                return std::get<std::shared_ptr<Series>>(it->second).get();
            }
        }
        return nullptr;
    }

    static std::string bytecodeToTxt(const Bytecode& bytecode);
    static Bytecode txtToBytecode(const std::string& txt);
private:
    /**
     * @using BuiltinFunction
     * @brief 定义了内置C++函数的函数签名。
     * @return Value 函数的计算结果。
     */
    using BuiltinFunction = std::function<Value(PineVM&)>;

    // --- 内部状态 ---
    Bytecode bytecode;
    const Instruction* ip = nullptr; // 指令指针
    std::vector<Value> stack;        // 操作数栈,
    std::vector<Value> globals;      // 全局变量存储槽
    std::vector<std::shared_ptr<Series>> vars;        // 中间变量存储槽

    // --- 执行上下文 ---
    int total_bars;
    int bar_index;

    // --- 内置环境 ---
    std::map<std::string, Value> built_in_vars;
    std::map<std::string, BuiltinFunction> built_in_funcs;
    std::map<std::string, std::shared_ptr<Series>> builtin_func_cache;

    // --- 结果存储 ---
    std::vector<PlottedSeries> plotted_series; // 用于存储 plot() 调用的结果

    // --- 私有辅助函数 ---

    /**
     * @brief 执行当前K线柱(bar_index)的字节码。
     */
    void runCurrentBar();

    Value& storeGlobal(int operand, const Value& val);
    
    double getNumericValue(const Value& val);
    bool getBoolValue(const Value& val);
    /**
     * @brief 注册所有由C++实现的内置函数 (如 ta.sma, input.int)。
     */
    void registerBuiltins();

    // 私有辅助函数，处理所有写入逻辑
    void writePlottedResultsToStream(std::ostream& stream) const;
};