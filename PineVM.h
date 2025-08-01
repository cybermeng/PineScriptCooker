// --- PineVM.h (修改后) ---

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

class PineVM; 

//-----------------------------------------------------------------------------
// FunctionContext 类 (The Function Call Context)
//-----------------------------------------------------------------------------
/**
 * @class FunctionContext
 * @brief 为内置函数调用提供一个安全、隔离的执行上下文。
 *        它负责管理参数传递，防止函数直接操作VM主堆栈，从而实现堆栈保护。
 */
class FunctionContext {
public:
    FunctionContext(PineVM& vm, std::shared_ptr<Series> result_series, std::vector<Value>&& args)
        : vm_(vm), result_series_(result_series), args_(std::move(args)) {}

    // --- 安全的参数访问接口 ---
    size_t argCount() const { return args_.size(); }
    
    const Value& getArg(size_t index) const {
        if (index >= args_.size()) {
            throw std::runtime_error("Argument index out of bounds: requested " + std::to_string(index) 
                                     + ", but only " + std::to_string(args_.size()) + " provided.");
        }
        return args_[index];
    }
    
    // --- 便利的类型转换接口 ---
    double getArgAsNumeric(size_t index) const;
    std::shared_ptr<Series> getArgAsSeries(size_t index) const;
    std::string getArgAsString(size_t index) const;

    // --- 访问VM核心状态的接口 ---
    int getCurrentBarIndex() const;
    std::shared_ptr<Series> getResultSeries() const { return result_series_; }
    PineVM& getVM() { return vm_; }

private:
    PineVM& vm_;
    std::shared_ptr<Series> result_series_; // 函数应该写入结果的序列
    std::vector<Value> args_;               // 本次调用的参数列表 (已从主堆栈弹出)
};

//-----------------------------------------------------------------------------
// PineVM 类 (The Virtual Machine Class)
//-----------------------------------------------------------------------------

/**
 * @class PineVM
 * @brief 一个用于执行 PineScript 字节码的堆栈式虚拟机，支持全量和增量计算。
 */
class PineVM {
public:
    /**
     * @brief PineVM 的构造函数。
     */
    PineVM(); // 不再需要 total_bars
    ~PineVM();

    /**
     * @brief 加载要执行的字节码，并重置VM状态，为新的计算做准备。
     * @param code 字节码的文本表示。
     */
    void loadBytecode(const std::string& code);
    
    /**
     * @brief 执行已加载的字节码，从当前 bar_index 计算到 new_total_bars。
     *        可用于批量初始计算和后续的增量计算。
     * @param new_total_bars 目标要计算到的总K线柱数量。
     * @return 0表示成功, 非0表示失败。
     * @example
     *   // 首次批量计算1000根K线
     *   vm.execute(1000);
     * 
     *   // 推送了1根新的K线数据后，进行增量计算
     *   // (假设用户已更新了 "close", "open" 等序列的第1000个索引的数据)
     *   vm.execute(1001); // 这次只会计算 bar_index = 1000
     */
    int execute(int new_total_bars);

    std::string getLastErrorMessage() const { return lastErrorMessage; }

  
    /**
     * @brief 获取当前正在执行的K线柱索引。
     * @return int 当前的 bar_index。
     */
    int getCurrentBarIndex() const { return bar_index; }

    int getTotalBars() const { return total_bars; }

    /**
     * @brief 获取所有全局变量（包括绘制的序列）。
     * @return const std::vector<Value>& 全局变量的引用。
     */
    const std::vector<Value>& getGlobalSeries() { return globals; }

    /**
     * @brief 打印所有已绘制的序列及其数据。
     */
    void printPlottedResults() const;

    void writePlottedResultsToFile(const std::string& filename, int precision = 3) const;
    
    std::string getPlottedResultsAsString(int precision = 3) const;

    void registerSeries(const std::string& name, std::shared_ptr<Series> series);
 
    /**
     * @brief 获取一个已注册的序列。这是更新输入数据的关键接口。
     * @param name 序列的名称 (例如 "open", "close")。
     * @return Series* 指向序列对象的原始指针，如果未找到则返回 nullptr。
     */
    Series* getSeries(const std::string& name) {
        auto it = built_in_vars.find(name);
        if (it != built_in_vars.end()) {
            if (std::holds_alternative<std::shared_ptr<Series>>(it->second)) {
                return std::get<std::shared_ptr<Series>>(it->second).get();
            }
        }
        return nullptr;
    }

    double getNumericValue(const Value& val);
    bool getBoolValue(const Value& val);

private:
    // --- 内部状态 ---
    Bytecode bytecode;

    std::string lastErrorMessage;
    const Instruction* ip = nullptr; // 指令指针
    std::vector<Value> stack;        // 操作数栈,
    std::vector<Value> globals;      // 全局变量存储槽
    std::vector<std::shared_ptr<Series>> vars;        // 中间变量存储槽
    std::map<std::string, ExportedSeries> exports;

    // --- 执行上下文 ---
    int total_bars; // 当前已知的总K线数
    int bar_index;  // 下一个要计算的K线索引

    using BuiltinFunction = std::function<Value(FunctionContext&)>;

    /**
     * @brief 存储内置函数的信息，包括其可接受的参数数量范围。
     */
    struct BuiltinInfo {
        BuiltinFunction function;
        int min_args; // 函数期望的最少参数数量
        int max_args; // 函数期望的最多参数数量
                      // 对于固定参数函数, min_args == max_args   
                      };
    std::map<std::string, Value> built_in_vars;
    std::map<std::string, BuiltinInfo> built_in_funcs;
    std::map<std::string, std::shared_ptr<Series>> builtin_func_cache;

    // --- 私有辅助函数 ---
    void runCurrentBar();
    Value pop();
    void push(Value val);
    void pushNumbericValue(double val, int operand);
    Value& storeGlobal(int operand, const Value& val);
    void writePlottedResultsToStream(std::ostream& stream, int precision = 3) const;
    void printSeriesSummary(const Series& series, std::function<void(double)> print_value) const;
    std::shared_ptr<Series> findTimeSeries() const;
    std::vector<std::shared_ptr<Series>> getAllPlottableSeries() const;

    void registerBuiltins();
    void registerBuiltinsHithink();
};