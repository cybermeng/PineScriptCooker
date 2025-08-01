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


// --- FunctionContext 方法实现 ---

double FunctionContext::getArgAsNumeric(size_t index) const {
    return vm_.getNumericValue(getArg(index));
}

std::shared_ptr<Series> FunctionContext::getArgAsSeries(size_t index) const {
    const Value& val = getArg(index);
    if (auto* p = std::get_if<std::shared_ptr<Series>>(&val)) {
        return *p;
    }
    throw std::runtime_error("Argument " + std::to_string(index) + " is not a Series.");
}

std::string FunctionContext::getArgAsString(size_t index) const {
    const Value& val = getArg(index);
    if (auto* p = std::get_if<std::string>(&val)) {
        return *p;
    }
    throw std::runtime_error("Argument " + std::to_string(index) + " is not a String.");
}

int FunctionContext::getCurrentBarIndex() const {
    return vm_.getCurrentBarIndex();
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
            auto it = built_in_funcs.find(func_name);
            if (it == built_in_funcs.end()) {
                 throw std::runtime_error("Undefined built-in function: " + func_name);
            }
            const auto& builtin_info = it->second;

            // 1. 弹出由编译器压入的 "实际参数数量"。
            //    这是新的调用约定：argN, ..., arg1, arg0, arg_count
            Value arg_count_val = pop();
            int actual_args = static_cast<int>(getNumericValue(arg_count_val));

            // 2. 验证实际参数数量是否在函数声明的范围内。
            if (actual_args < builtin_info.min_args || actual_args > builtin_info.max_args) {
                std::string expected;
                if (builtin_info.min_args == builtin_info.max_args) {
                    expected = std::to_string(builtin_info.min_args);
                } else {
                    expected = "between " + std::to_string(builtin_info.min_args) + 
                               " and " + std::to_string(builtin_info.max_args);
                }
                throw std::runtime_error("Invalid number of arguments for '" + func_name + "'. "
                                         "Expected " + expected + " arguments, but got " 
                                         + std::to_string(actual_args) + ".");
            }

            // 3. 检查堆栈深度是否足够。
            //    (现在栈上应该有 `actual_args` 个参数)
            if (stack.size() < actual_args) {
                throw std::runtime_error("Stack underflow during call to '" + func_name + "'. "
                                         "Not enough values on stack for " + std::to_string(actual_args) + " arguments.");
            }

            // 4. 弹出结果序列和所有实际参数。
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

            std::vector<Value> args;
            args.reserve(actual_args);
            for (int i = 0; i < actual_args; ++i) {
                args.push_back(pop());
            }
            std::reverse(args.begin(), args.end()); // 恢复参数顺序

            // 5. 创建上下文并调用函数。
            FunctionContext context(*this, result_series, std::move(args));
            Value result = builtin_info.function(context);
            
            // 6. 将最终结果压栈。
            push(result);
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
    // `input` 函数，可以接受1个或2个参数
    built_in_funcs["input.int"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // 参数1: defval (必须)
            double defval = ctx.getArgAsNumeric(0);
            
            // 参数2: title (可选)
            std::string title;
            if (ctx.argCount() > 1) {
                title = ctx.getArgAsString(1);
            } else {
                title = "Default Title"; // 为可选参数提供默认值
            }

            // 这里可以处理 title，例如用于日志或元数据
            // std::cout << "Input with title: " << title << std::endl;
            
            int current_bar = ctx.getCurrentBarIndex();
            std::shared_ptr<Series> result_series = ctx.getResultSeries();
            result_series->setCurrent(current_bar, defval);
            return result_series;
        },
        .min_args = 1, // 至少需要1个参数
        .max_args = 2  // 最多接受2个参数
    };
     built_in_funcs["indicator"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // indicator 函数不进行实际计算，只用于元数据。
            // 它通常用于设置脚本的名称、叠加等属性。
            // 在VM中，我们只返回一个monostate或一个默认值，不影响执行流。
            // 参数1: title (必须)
            std::string title = ctx.getArgAsString(0);
            
            // 参数2: overlay (可选)
            bool overlay = true; // 默认值
            if (ctx.argCount() > 1) {
                overlay = ctx.getArgAsNumeric(1) != 0.0; // 0.0 为 false, 非0为 true
            }

            // 在这里可以处理 title 和 overlay，例如存储在 VM 状态中
            // vm_.setScriptTitle(title);
            // vm_.setScriptOverlay(overlay);

            // indicator 函数通常不返回一个可用于后续计算的值
            // 所以我们返回一个 monostate 或者一个表示成功的布尔值
            // 这里返回一个 monostate，表示“无值”
            return std::monostate{};
            
        },
        .min_args = 1,
        .max_args = 2
    };
   
    // `plot` 函数，我们也可以让 color 可选
    built_in_funcs["plot1"] = 
    built_in_funcs["plot2"] = 
    built_in_funcs["plot3"] = 
    built_in_funcs["plot4"] = 
    built_in_funcs["plot5"] = 
    built_in_funcs["plot"] = {
        .function = [](FunctionContext &ctx) -> Value {
            auto plot_series = ctx.getArgAsSeries(0);
            
            std::string color;
            if (ctx.argCount() > 1) {
                color = ctx.getArgAsString(1);
            } else {
                color = "blue"; // 默认颜色
            }

            int current_bar = ctx.getCurrentBarIndex();
            std::shared_ptr<Series> result_series = ctx.getResultSeries();
            PineVM& vm = ctx.getVM();

            auto it = vm.exports.find(result_series->name);
            if (it == vm.exports.end()) {
                vm.exports[result_series->name] = {result_series->name, color};
            }
            result_series->setCurrent(current_bar, plot_series->getCurrent(current_bar));
            return result_series;
        },
        .min_args = 1,
        .max_args = 2
    };
    built_in_funcs["ta.sma"] = {
        .function = [](FunctionContext &ctx) -> Value {
            auto series = ctx.getArgAsSeries(0);
            double length = ctx.getArgAsNumeric(1);
            int current_bar = ctx.getCurrentBarIndex();
            std::shared_ptr<Series> result_series = ctx.getResultSeries();

            if (current_bar < length - 1) {
                result_series->setCurrent(current_bar, NAN);
                return result_series;
            }

            double sum = 0.0;
            int count = 0;
            for (int i = 0; i < length; ++i) {
                double val = series->getCurrent(current_bar - i);
                if (!std::isnan(val)) {
                    sum += val;
                    count++;
                }
            }
            if (count > 0) {
                result_series->setCurrent(current_bar, sum / count);
            } else {
                result_series->setCurrent(current_bar, NAN);
            }
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };
    built_in_funcs["ta.ema"] = {
        .function = [](FunctionContext &ctx) -> Value {
            auto series = ctx.getArgAsSeries(0);
            double length = ctx.getArgAsNumeric(1);
            int current_bar = ctx.getCurrentBarIndex();
            std::shared_ptr<Series> result_series = ctx.getResultSeries();
            
            if (current_bar == 0) {
                result_series->setCurrent(current_bar, series->getCurrent(current_bar));
                return result_series;
            }

            double alpha = 2.0 / (length + 1.0);
            double current_value = series->getCurrent(current_bar);
            double prev_ema = result_series->getCurrent(current_bar - 1);

            if (std::isnan(current_value) || std::isnan(prev_ema)) {
                // If either current value or previous EMA is NaN, the result is NaN
                // Or, if it's the first bar and current_value is NaN, it should be NaN
                result_series->setCurrent(current_bar, NAN);
            } else {
                double ema = (current_value - prev_ema) * alpha + prev_ema;
                result_series->setCurrent(current_bar, ema);
            }
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };
    built_in_funcs["rsi"] = 
    built_in_funcs["ta.rsi"] = {
        .function = [](FunctionContext &ctx) -> Value {
            auto series = ctx.getArgAsSeries(0);
            double length = ctx.getArgAsNumeric(1);
            int current_bar = ctx.getCurrentBarIndex();
            std::shared_ptr<Series> result_series = ctx.getResultSeries();
            PineVM& vm = ctx.getVM();

            if (current_bar == 0) {
                result_series->setCurrent(current_bar, NAN);
                return result_series;
            }

            // 获取前一个 bar 的增益和损失
            std::string cache_key = "__call__ta.rsi__gain";
            std::shared_ptr<Series> rsi__gain_series;
            if (vm.builtin_func_cache.count(cache_key))
            {
                rsi__gain_series = vm.builtin_func_cache.at(cache_key);
            }
            else
            {
                rsi__gain_series = std::make_shared<Series>();
                rsi__gain_series->name = cache_key;
                vm.builtin_func_cache[cache_key] = rsi__gain_series;
            }
            cache_key = "__call__ta.rsi__loss";
            std::shared_ptr<Series> rsi__loss_series;
            if (vm.builtin_func_cache.count(cache_key))
            {
                rsi__loss_series = vm.builtin_func_cache.at(cache_key);
            }
            else
            {
                rsi__loss_series = std::make_shared<Series>();
                rsi__loss_series->name = cache_key;
                vm.builtin_func_cache[cache_key] = rsi__loss_series;
            }
            double prev_gain = rsi__gain_series->getCurrent(current_bar - 1);
            double prev_loss = rsi__loss_series->getCurrent(current_bar - 1);

            // 计算当前 bar 的价格变化
            double current_price = series->getCurrent(current_bar);
            double prev_price = series->getCurrent(current_bar - 1);

            if (std::isnan(current_price) || std::isnan(prev_price)) {
                result_series->setCurrent(current_bar, NAN);
                return result_series;
            }

            double change = current_price - prev_price;
            double gain = change > 0 ? change : 0;
            double loss = change < 0 ? -change : 0;

            double avg_gain, avg_loss;

            if (current_bar < length) {
                // 初始阶段，简单累加
                avg_gain = prev_gain + gain;
                avg_loss = prev_loss + loss;
            } else {
                // 使用平滑平均 (类似 EMA)
                avg_gain = (prev_gain * (length - 1) + gain) / length;
                avg_loss = (prev_loss * (length - 1) + loss) / length;
            }
            // 缓存增益和损失，供下一次迭代使用
            rsi__gain_series->setCurrent(current_bar, avg_gain);
            rsi__loss_series->setCurrent(current_bar, avg_loss);

            double rs = (avg_loss == 0) ? (avg_gain / 0.0000000001) : (avg_gain / avg_loss); // 避免除以零
            double rsi = 100 - (100 / (1 + rs));

            result_series->setCurrent(current_bar, rsi);
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };
    //
    registerBuiltinsHithink();
}