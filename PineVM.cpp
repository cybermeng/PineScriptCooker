#include "PineVM.h"
#include <iostream>
#include <iomanip>
#include <numeric>
#include <algorithm> // For std::find_if
#include <fstream>   // For file output
#include <sstream>   // For std::stringstream
#include <map>       // For opCodeMap in txtToBytecode
#include <optional>  // For std::optional in txtToBytecode
#include <cstdint>   // for uint64_t

double Series::getCurrent(int bar_index)
{
    if (bar_index >= 0 && bar_index < data.size())
    {
        return data[bar_index];
    }
    // 如果索引超出范围或数据未加载，返回 NaN
    return NAN;
}

// ... Series::setCurrent 保持不变 ...
void Series::setCurrent(int bar_index, double value)
{
    if (bar_index >= data.size())
    {
        data.resize(bar_index + 1, NAN);
    }
    data[bar_index] = value;
}

void Series::setName(const std::string &name)
{
    this->name = name;
}

PineVM::PineVM()
    : total_bars(0), bar_index(0), ip(nullptr)
{
    registerBuiltins();
}

PineVM::~PineVM()
{
}

// loadBytecode 现在负责加载代码并重置整个VM的状态
void PineVM::loadBytecode(const std::string &code)
{
    std::cout << "----- Loading bytecode and resetting VM -----" << std::endl;
    bytecode = txtToBytecode(code);
    std::cout << bytecodeToTxt(bytecode);

    // 重置所有计算状态，为新的执行做准备
    globals.clear();
    globals.resize(bytecode.global_name_pool.size());
    exports.clear();

    vars.clear();
    vars.reserve(bytecode.varNum);
    for (int i = 0; i < bytecode.varNum; ++i)
    {
        auto temp_series = std::make_shared<Series>();
        temp_series->name = "_tmp" + std::to_string(i);
        vars.push_back(temp_series);
    }
    
    // 清空绘图结果和函数状态缓存
    builtin_func_cache.clear();

    // 重置执行上下文
    bar_index = 0;
    total_bars = 0;
}

// execute 现在可以处理批量和增量计算
int PineVM::execute(int new_total_bars)
{
    // 如果新的目标K线数不大于当前已计算的K线数，则无需操作
    if (new_total_bars <= this->bar_index) {
        // std::cout << "No new bars to execute. Current index: " << bar_index 
        //           << ", requested total: " << new_total_bars << std::endl;
        return 0; // 不是错误，只是无事可做
    }

    this->total_bars = new_total_bars;

    try
    {
        // 循环从当前 bar_index 继续，直到达到新的 total_bars
        // 这个 for 循环的结构是实现增量计算的关键
        for (; bar_index < this->total_bars; ++bar_index)
        {
            // std::cout << "--- Executing Bar #" << bar_index << " ---" << std::endl;
            runCurrentBar();
        }
    }
    catch (const std::exception &e)
    {
        // 错误发生时，bar_index 会停留在出错的K线上，方便调试
        std::stringstream ss;
        ss << "PineVM::execute Error: " << e.what()
                  << " @bar_index: " << bar_index
                  << " @ip: " << (ip ? std::to_string(ip - &bytecode.instructions[0]) : "null")
                  << std::endl;
        lastErrorMessage = ss.str();
        return 1;
    }

    // 所有请求的K线都成功执行
    return 0;
}

void PineVM::push(Value val)
{
    stack.push_back(std::move(val));
}

Value PineVM::pop()
{
    if (stack.empty())
        throw std::runtime_error("Stack underflow!");
    Value val = stack.back();
    stack.pop_back();
    return val;
}

double PineVM::getNumericValue(const Value &val)
{
    if (auto *p = std::get_if<double>(&val))
    {
        return *p;
    }
    if (auto *p = std::get_if<std::shared_ptr<Series>>(&val))
    {
        return *p ? (*p)->getCurrent(bar_index) : NAN;
    }
    if (std::holds_alternative<bool>(val))
    {
        return static_cast<double>(std::get<bool>(val));
    }
    if (std::holds_alternative<std::monostate>(val))
    {
        return NAN;
    }
    throw std::runtime_error("Unsupported operand type for numeric operation.");
}

bool PineVM::getBoolValue(const Value &val)
{
    if (auto *p = std::get_if<bool>(&val))
    {
        return *p;
    }
    if (auto *p = std::get_if<std::shared_ptr<Series>>(&val))
    {
        return *p ? static_cast<bool>((*p)->getCurrent(bar_index)) : NAN;
    }
    if (std::holds_alternative<double>(val))
    {
        return static_cast<bool>(std::get<double>(val));
    }
    if (std::holds_alternative<std::monostate>(val))
    {
        return NAN;
    }
    throw std::runtime_error("Unsupported operand type for bool operation.");
}

void PineVM::pushNumbericValue(double val, int operand)
{
    // 这个函数处理算术/逻辑运算的结果。
    // 在PineScript中，这些操作通常产生一个新的序列。
    // 'operand' 参数是编译器分配的中间变量槽的索引，
    // 用于存储这个新序列。

    // 检查操作数是否是有效的中间变量索引
    if (operand < 0 || operand >= vars.size())
    {
        // 这是一个严重的字节码错误。如果一个操作需要存储中间结果，
        // 编译器必须提供一个有效的槽位索引。
        // 一个简单的回退方案是直接压入double值，但这会破坏序列的连续性。
        // push(val); // 这是一个不正确的简化方案
        throw std::runtime_error("Invalid intermediate variable index (" + std::to_string(operand) + ") for arithmetic/logic operation. Max index is " + std::to_string(vars.size() - 1) + ".");
    }

    // 1. 从预先分配的池中获取中间序列
    auto &result_series = vars[operand];

    // 2. 为当前K线柱设置计算出的值
    result_series->setCurrent(bar_index, val);

    // 3. 将这个（现在已更新的）序列的智能指针压入栈中，
    // 以便后续操作（如另一个算术运算或存储到全局变量）可以使用它。
    push(result_series);
}

Value &PineVM::storeGlobal(int operand, const Value &val)
{
    // 检查全局变量槽位是否已经是一个Series
    if (std::holds_alternative<std::shared_ptr<Series>>(globals[operand]))
    {
        auto series_ptr = std::get<std::shared_ptr<Series>>(globals[operand]);
        if (std::holds_alternative<double>(val) || std::holds_alternative<bool>(val))
        {
            // 如果弹出的值是double，则设置Series的当前bar值
            series_ptr->setCurrent(bar_index,
                                   std::holds_alternative<bool>(val) ? static_cast<double>(std::get<bool>(val)) : std::get<double>(val));
        }
        else if (std::holds_alternative<std::shared_ptr<Series>>(val))
        {
            // 如果弹出的值是Series，则替换bar对应数值
            auto val_series_ptr = std::get<std::shared_ptr<Series>>(val);
            series_ptr->setCurrent(bar_index, val_series_ptr->getCurrent(bar_index));
        }
        else
        {
            // 尝试将其他类型的值存储到Series中（可能需要转换或抛出错误）
            throw std::runtime_error("Attempted to store unsupported type into existing Series global.");
        }
    }
    else if (std::holds_alternative<std::monostate>(globals[operand]))
    {
        // 如果是monostate，说明这个槽位是空的，可以根据弹出的值类型来初始化
        if (std::holds_alternative<double>(val) || std::holds_alternative<bool>(val))
        {
            // 如果是double/bool，创建一个新的Series来存储它
            auto new_series = std::make_shared<Series>();
            new_series->setCurrent(bar_index,
                                   std::holds_alternative<bool>(val) ? static_cast<double>(std::get<bool>(val)) : std::get<double>(val));
            new_series->setName(bytecode.global_name_pool[operand]);
            globals[operand] = new_series;
        }
        else
        {
            // 其他类型直接存储
            globals[operand] = val;
            auto series_ptr = std::get<std::shared_ptr<Series>>(globals[operand]);
            series_ptr->setName(bytecode.global_name_pool[operand]);
        }
    }
    else
    {
        // 如果不是Series，直接存储弹出的值
        globals[operand] = val;
        auto series_ptr = std::get<std::shared_ptr<Series>>(globals[operand]);
        series_ptr->setName(bytecode.global_name_pool[operand]);
    }
    return globals[operand];
}

void PineVM::runCurrentBar()
{
    ip = &bytecode.instructions[0];

    while (ip->op != OpCode::HALT)
    {
        switch (ip->op)
        {
        case OpCode::PUSH_CONST:
        {
            push(bytecode.constant_pool[ip->operand]);
            break;
        }
        case OpCode::POP:
        {
            pop();
            break;
        }
        case OpCode::SUBSCRIPT: // 新增：处理下标操作
        {
            Value index_val = pop();
            Value callee_val = pop();

            int offset = static_cast<int>(getNumericValue(index_val));
            
            auto* series_ptr = std::get_if<std::shared_ptr<Series>>(&callee_val);
            if (!series_ptr || !*series_ptr) {
                 // 如果被索引的不是一个有效的序列，则结果为 NaN
                pushNumbericValue(NAN, ip->operand);
            } else {
                double result = (*series_ptr)->getCurrent(bar_index - offset);
                pushNumbericValue(result, ip->operand);
            }
            break;
        }
        case OpCode::ADD:
        case OpCode::SUB:
        case OpCode::MUL:
        case OpCode::DIV:
        case OpCode::LESS:
        case OpCode::LESS_EQUAL:
        case OpCode::EQUAL_EQUAL:
        case OpCode::BANG_EQUAL:
        case OpCode::GREATER:
        case OpCode::GREATER_EQUAL:
        case OpCode::LOGICAL_AND:
        case OpCode::LOGICAL_OR:
        {
            double right = getNumericValue(pop());
            double left = getNumericValue(pop());
            if(!std::isnan(left) && !std::isnan(right))
            {
                if (ip->op == OpCode::ADD)
                {
                    pushNumbericValue(left + right, ip->operand);
                }
                else if (ip->op == OpCode::DIV)
                {
                    if (right == 0.0)
                    {
                        pushNumbericValue(NAN, ip->operand);
                    }
                    else
                        pushNumbericValue(left / right, ip->operand);
                }
                else if (ip->op == OpCode::SUB)
                    pushNumbericValue(left - right, ip->operand);
                else if (ip->op == OpCode::MUL)
                    pushNumbericValue(left * right, ip->operand);
                else if (ip->op == OpCode::DIV)
                    pushNumbericValue(left / right, ip->operand);
                else if (ip->op == OpCode::LESS)
                    pushNumbericValue(left < right, ip->operand);
                else if (ip->op == OpCode::LESS_EQUAL)
                    pushNumbericValue(left <= right, ip->operand);
                else if (ip->op == OpCode::EQUAL_EQUAL)
                    pushNumbericValue(left == right, ip->operand);
                else if (ip->op == OpCode::BANG_EQUAL)
                    pushNumbericValue(left != right, ip->operand);
                else if (ip->op == OpCode::GREATER)
                    pushNumbericValue(left > right, ip->operand);
                else if (ip->op == OpCode::GREATER_EQUAL)
                    pushNumbericValue(left >= right, ip->operand);
                else if (ip->op == OpCode::LOGICAL_AND)
                {
                    // 在Hithink中, 非0且非NaN为true, 结果为1.0或0.0
                    bool result = (left != 0.0 && right != 0.0);
                    pushNumbericValue(result ? 1.0 : 0.0, ip->operand);
                }
                else if (ip->op == OpCode::LOGICAL_OR)
                {
                    // 在Hithink中, 非0且非NaN为true, 结果为1.0或0.0
                    bool result = (left != 0.0 || right != 0.0);
                    pushNumbericValue(result ? 1.0 : 0.0, ip->operand);
                }
            }
            else
            {
                pushNumbericValue(NAN, ip->operand);
            }
            break;
        }
        case OpCode::LOAD_GLOBAL:
        {
            push(globals[ip->operand]);
            break;
        }
        case OpCode::STORE_GLOBAL:
        {
            storeGlobal(ip->operand, pop());
            break;
        }
        case OpCode::STORE_EXPORT:
        {
            std::string& name = bytecode.global_name_pool[ip->operand];
            auto it = exports.find(name);
            if (it == exports.end())
            {
                exports[name] = {name, "default_color"};
            }
            storeGlobal(ip->operand, pop());
            break;
        }
        case OpCode::RENAME_SERIES:
        {
            Value name_val = pop();
            Value &series_val = stack.back(); // Peek at the top of the stack
            auto series_ptr = std::get<std::shared_ptr<Series>>(series_val);
            series_ptr->name = std::get<std::string>(name_val);
            break;
        }
        case OpCode::LOAD_BUILTIN_VAR:
        {
            const std::string &name = std::get<std::string>(bytecode.constant_pool[ip->operand]);
            if (built_in_vars.count(name))
            {
                push(built_in_vars.at(name));
            }
            else
            {
                throw std::runtime_error("Undefined built-in variable: " + name);
            }
            break;
        }
        case OpCode::JUMP_IF_FALSE:
        {
            Value condition = pop();
            if (!std::get<bool>(condition))
            {
                ip += ip->operand; // Jump forward
                continue;          // Skip the default ip++
            }
            break;
        }
        case OpCode::JUMP:
            ip += ip->operand; // Jump forward
            break;
        case OpCode::CALL_BUILTIN_FUNC:
        {
            const std::string &func_name = std::get<std::string>(bytecode.constant_pool[ip->operand]);
            if (built_in_funcs.count(func_name))
            {
                // 基于函数和序号创建唯一的缓存键，以支持状态保持
                std::string cache_key = "__call__" + func_name + "__" + std::to_string(ip->operand);
                std::shared_ptr<Series> result_series;
                if (builtin_func_cache.count(cache_key))
                {
                    result_series = builtin_func_cache.at(cache_key);
                }
                else
                {
                    result_series = std::make_shared<Series>();
                    result_series->name = cache_key;
                    builtin_func_cache[cache_key] = result_series;
                }
                push(result_series);
                Value result = built_in_funcs.at(func_name)(*this);
                push(result);
            }
            else
            {
                throw std::runtime_error("Undefined built-in function: " + func_name);
            }
            break;
        }
        default:
            throw std::runtime_error("Unknown opcode!");
        }
        ip++;
    }
}

void PineVM::registerSeries(const std::string &name, std::shared_ptr<Series> series)
{
    built_in_vars[name] = series;
}

/**
 * @brief 查找并返回 "time" 序列。如果不存在则返回 nullptr。
 */
std::shared_ptr<Series> PineVM::findTimeSeries() const {
    for (const auto &pair : built_in_vars) {
        if (pair.first == "time" && std::holds_alternative<std::shared_ptr<Series>>(pair.second)) {
            return std::get<std::shared_ptr<Series>>(pair.second);
        }
    }
    return nullptr;
}


/**
 * @brief 收集所有需要绘制的 Series (在 exports 中声明的)。
 * 这是核心的重构，将分散在各处的遍历逻辑统一起来。
 * @return 包含所有可绘制 Series 指针的向量。
 */
std::vector<std::shared_ptr<Series>> PineVM::getAllPlottableSeries() const {
    std::vector<std::shared_ptr<Series>> plottable_series;

    // 1. 从 globals 中收集
    for (const auto &plotted : globals) {
        if (std::holds_alternative<std::shared_ptr<Series>>(plotted)) {
            auto series = std::get<std::shared_ptr<Series>>(plotted);
            if (exports.count(series->name)) {
                plottable_series.push_back(series);
            }
        }
    }

    // 2. 从 builtin_func_cache 中收集
    for (const auto &pair : builtin_func_cache) {
        if (exports.count(pair.first)) {
            plottable_series.push_back(pair.second);
        }
    }

    return plottable_series;
}


/**
 * @brief 打印单个 Series 的数据摘要 (前10个和后10个值)。
 * @param series 要打印的序列。
 * @param print_value 一个函数，用于定义如何打印单个数据点。
 */
void PineVM::printSeriesSummary(const Series& series, std::function<void(double)> print_value) const {
    const auto &data = series.data;
    const size_t n = data.size();

    std::cout << "  Data (total " << n << " points): [";

    if (n <= 20) {
        for (size_t i = 0; i < n; ++i) {
            if (i > 0) std::cout << ", ";
            print_value(data[i]);
        }
    } else {
        for (size_t i = 0; i < 10; ++i) {
            if (i > 0) std::cout << ", ";
            print_value(data[i]);
        }
        std::cout << ", ...";
        for (size_t i = n - 10; i < n; ++i) {
            std::cout << ", ";
            print_value(data[i]);
        }
    }
    std::cout << "]" << std::endl;
}


// --- 重构后的公共接口实现 ---

void PineVM::printPlottedResults() const {
    if (exports.empty()) {
        std::cout << "\n--- No Plotted Results ---" << std::endl;
        return;
    }

    // 1. 打印时间序列
    if (auto time_series = findTimeSeries()) {
        std::cout << "\n--- Time Series (前10个和后10个值) ---" << std::endl;
        auto print_time_value = [](double val) {
            if (std::isnan(val)) {
                std::cout << "nan";
            } else {
                time_t rawtime = static_cast<time_t>(val);
                struct tm dt;
                #ifdef _WIN32
                    localtime_s(&dt, &rawtime);
                #else
                    localtime_r(&rawtime, &dt);
                #endif
                std::cout << std::put_time(&dt, "%Y-%m-%d %H:%M:%S");
            }
        };
        printSeriesSummary(*time_series, print_time_value);
    }

    // 2. 打印所有其他可绘制序列
    std::vector<std::shared_ptr<Series>> all_series = getAllPlottableSeries();
    if (all_series.empty()) {
        // 可能只有时间序列，没有其他 plot
        return;
    }

    std::cout << "\n--- Plotted Results (前10个和后10个值) ---" << std::endl;
    auto print_numeric_value = [](double val) {
        if (std::isnan(val)) {
            std::cout << "nan";
        } else {
            std::cout << std::fixed << std::setprecision(3) << val;
        }
    };

    for (const auto &series : all_series) {
        std::cout << "Series: " << series->name << std::endl;
        printSeriesSummary(*series, print_numeric_value);
    }
}


// --- 重构后的私有核心逻辑 (写入流) ---

void PineVM::writePlottedResultsToStream(std::ostream &stream, int precision) const {
    if (exports.empty()) {
        // 虽然 print 函数有这个检查，但这里保留以确保独立性
        // 可以根据需要决定是否输出提示信息
        return;
    }

    auto time_series = findTimeSeries();
    auto plottable_series = getAllPlottableSeries();

    if (!time_series && plottable_series.empty()) {
        return; // 无任何数据可写
    }

    // 1. 写入CSV头
    bool first_column = true;
    if (time_series) {
        stream << "time";
        first_column = false;
    }
    for (const auto &series : plottable_series) {
        if (!first_column) stream << ",";
        stream << series->name;
        first_column = false;
    }
    stream << "\n";

    // 2. 计算最大行数
    size_t max_rows = 0;
    if (time_series) {
        max_rows = time_series->data.size();
    }
    for (const auto &series : plottable_series) {
        max_rows = std::max(max_rows, series->data.size());
    }

    // 3. 逐行写入数据
    for (size_t i = 0; i < max_rows; ++i) {
        first_column = true;
        
        // 写入时间列
        if (time_series) {
            if (i < time_series->data.size()) {
                double val = time_series->data[i];
                if (!std::isnan(val) && val > 0) {
                    time_t rawtime = static_cast<time_t>(val);
                    struct tm dt;
                    #ifdef _WIN32
                        localtime_s(&dt, &rawtime);
                    #else
                        localtime_r(&rawtime, &dt);
                    #endif
                    stream << std::put_time(&dt, "%Y-%m-%d %H:%M:%S");
                }
            }
            first_column = false;
        }

        // 写入其他数据列
        for (const auto &series : plottable_series) {
            if (!first_column) stream << ",";
            if (i < series->data.size()) {
                double val = series->data[i];
                if (std::isnan(val)) {
                     stream << "nan";
                } else {
                     stream << std::fixed << std::setprecision(precision) << val;
                }
            }
            first_column = false;
        }
        stream << "\n";
    }
}

// --- 其余公共接口 (无需修改，它们已经设计得很好) ---

void PineVM::writePlottedResultsToFile(const std::string &filename, int precision) const {
    std::ofstream outfile(filename);
    if (!outfile.is_open()) {
        std::cerr << "Error: Could not open file " << filename << " for writing." << std::endl;
        return;
    }
    writePlottedResultsToStream(outfile, precision);
    outfile.close();
    std::cout << "Plotted results written to " << filename << std::endl;
}

std::string PineVM::getPlottedResultsAsString(int precision) const {
    std::stringstream ss;
    writePlottedResultsToStream(ss, precision);
    return ss.str();
}

void PineVM::registerBuiltins()
{ 
    built_in_funcs["input.int"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        vm.pop();
        Value defval = vm.pop();
        result_series->setCurrent(current_bar, std::get<double>(defval));
        return result_series;
    };
    built_in_funcs["plot"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value color = vm.pop();
        Value val = vm.pop();
        auto it = vm.exports.find(result_series->name);
        if (it == vm.exports.end())
        {
            vm.exports[result_series->name] = {result_series->name, std::get<std::string>(color)};
        }
        auto* plot_series = std::get_if<std::shared_ptr<Series>>(&val);
        if(plot_series)
        {
            result_series->setCurrent(current_bar, (*plot_series)->getCurrent(current_bar));
        }
        return result_series;
    };
    built_in_funcs["ta.sma"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto source_series = std::get<std::shared_ptr<Series>>(source_val);

        // ta.sma 的实现
        // 确保有足够的历史数据来计算SMA
        if (current_bar < length - 1)
        {
            result_series->setCurrent(current_bar, NAN);
        }
        else
        {
            double sum = 0.0;
            bool has_nan = false;
            for (int i = 0; i < length; ++i)
            {
                double val = source_series->getCurrent(current_bar - i);
                if (std::isnan(val))
                {
                    has_nan = true;
                    break;
                }
                sum += val;
            }

            if (has_nan)
            {
                result_series->setCurrent(current_bar, NAN);
            }
            else
            {
                result_series->setCurrent(current_bar, sum / length);
            }
        }
        return result_series;
    };
    built_in_funcs["ta.rsi"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto source_series = std::get<std::shared_ptr<Series>>(source_val);

        // RSI 的计算需要前一天的收盘价来计算涨跌幅
        // 因此，我们需要至少两天的历史数据才能开始计算 RSI
        if (current_bar == 0)
        {
            result_series->setCurrent(current_bar, NAN);
            return result_series;
        }

        // 计算前 length 周期内的平均涨幅 (avg_gain) 和平均跌幅 (avg_loss)
        double avg_gain = 0.0;
        double avg_loss = 0.0;
        int valid_count = 0;

        for (int i = 0; i <= current_bar; ++i)
        {
            if (i == 0)
            { // 第一根K线没有前一日数据，跳过
                continue;
            }

            double current_close = source_series->getCurrent(current_bar - i);
            double prev_close = source_series->getCurrent(current_bar - i + 1);

            if (std::isnan(current_close) || std::isnan(prev_close))
            {
                continue;
            }

            double change = current_close - prev_close;
            if (change > 0)
            {
                avg_gain += change;
            }
            else
            {
                avg_loss += std::abs(change);
            }
            valid_count++;

            if (valid_count >= length)
            {
                break; // 收集到足够的数据
            }
        }

        if (valid_count < length)
        {
            result_series->setCurrent(current_bar, NAN); // 数据不足
        }
        else
        {
            // 第一次计算RSI，使用简单平均
            avg_gain /= length;
            avg_loss /= length;

            double rs = (avg_loss == 0) ? (avg_gain / 1e-10) : (avg_gain / avg_loss); // 避免除以零
            double rsi = 100.0 - (100.0 / (1.0 + rs));
            result_series->setCurrent(current_bar, rsi);
        }
        return result_series;
    };
    //
    registerBuiltinsHithink();
}