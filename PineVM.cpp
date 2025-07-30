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
    /////////////////////////////////////////////////////////////////////////////////////////////
    //引用函数
    built_in_funcs["ama"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        // 函数说明：	求自适应均线值
        // 未来函数：	否
        // 函数用法：	AMA(X,A),A为自适应系数,必须小于1
        // 示例代码：	
        // 求自适应均线值:AMA(X,A);

        // 用法:
        // AMA(X,A),A为自适应系数,必须小于1
        // 算法:
        // Y=Y'+A*(X-Y').初值为X
        Value alpha_val = vm.pop();
        Value source_val = vm.pop();

        double alpha = vm.getNumericValue(alpha_val);
        auto source_series = std::get<std::shared_ptr<Series>>(source_val);

        double current_source_val = source_series->getCurrent(current_bar);
        double prev_ama = result_series->getCurrent(current_bar - 1);

        double ama_val;
        if (std::isnan(current_source_val))
        {
            ama_val = NAN;
        }
        else if (std::isnan(prev_ama))
        {
            // AMA的第一个值通常是NaN，或者用当前值播种
            ama_val = current_source_val;
        }
        else
        {
            // AMA计算公式: Y = Y' + A * (X - Y')
            ama_val = prev_ama + alpha * (current_source_val - prev_ama);
        }
        result_series->setCurrent(current_bar, ama_val);
        
        return result_series;
    };
    built_in_funcs["barscount"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        result_series->setCurrent(current_bar, static_cast<double>(current_bar + 1));
        return result_series;
    };
    built_in_funcs["barslast"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        // BARSLAST 上一条件成立位置
        // 上一次条件成立到当前的周期数.
        // 用法:
        // BARSLAST(X):上一次X不为0到现在的周期数
        Value condition_val = vm.pop();
        auto condition_series = std::get<std::shared_ptr<Series>>(condition_val);

        double barslast_val = NAN;
        for (int i = 0; i <= current_bar; ++i)
        {
            double val = condition_series->getCurrent(current_bar - i);
            if (!std::isnan(val) && val != 0.0)
            { // 找到第一个非零（true）的K线
                barslast_val = static_cast<double>(i);
                break;
            }
        }
        result_series->setCurrent(current_bar, barslast_val);
        return result_series;
    };
    built_in_funcs["barslastcount"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value condition_val = vm.pop();
        
        int current_bar = vm.getCurrentBarIndex();
        auto condition_series = std::get<std::shared_ptr<Series>>(condition_val);

        int count = 0;
        for (int i = current_bar; i >= 0; --i)
        {
            double val = condition_series->getCurrent(i);
            if (!std::isnan(val) && val != 0.0)
            { // 找到第一个非零（true）的K线
                count++;
            }
            else
            {
                break; // 遇到零或NaN，停止计数
            }
        }
        result_series->setCurrent(current_bar, static_cast<double>(count));
        return result_series;
    };
    built_in_funcs["barssince"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value condition_val = vm.pop();

        int current_bar = vm.getCurrentBarIndex();
        auto condition_series = std::get<std::shared_ptr<Series>>(condition_val);

        int bars_since = -1; // -1 表示从未发生
        for (int i = 0; i <= current_bar; ++i)
        {
            double val = condition_series->getCurrent(current_bar - i);
            if (!std::isnan(val) && val != 0.0)
            { // 找到第一个非零（true）的K线
                bars_since = i;
                break;
            }
        }
        result_series->setCurrent(current_bar, static_cast<double>(bars_since));
        return result_series;
    };
    built_in_funcs["barssincen"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value condition_val = vm.pop();

        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto condition_series = std::get<std::shared_ptr<Series>>(condition_val);

        int bars_since = -1; // -1 表示从未发生
        int count = 0;
        for (int i = 0; i <= current_bar; ++i)
        {
            double val = condition_series->getCurrent(current_bar - i);
            if (!std::isnan(val) && val != 0.0)
            { // 找到第一个非零（true）的K线
                bars_since = i;
                count++;
                if (count >= length)
                { // 已经找到足够多的满足条件的K线
                    break;
                }
            }
        }
        result_series->setCurrent(current_bar, static_cast<double>(bars_since));
        return result_series;
    };
    built_in_funcs["barsstatus"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value condition_val = vm.pop();
        int current_bar = vm.getCurrentBarIndex();
        auto condition_series = std::get<std::shared_ptr<Series>>(condition_val);

        // barsstatus(COND) 返回从当前K线开始，连续满足COND条件的K线数量。
        // 如果当前K线不满足条件，则返回0。
        int count = 0;
        for (int i = current_bar; i >= 0; --i)
        {
            double val = condition_series->getCurrent(i);
            if (!std::isnan(val) && val != 0.0)
            {
                count++;
            }
            else
            {
                break; // 遇到不满足条件的K线，停止计数
            }
        }
        result_series->setCurrent(current_bar, static_cast<double>(count));
        return result_series;
    };
    built_in_funcs["const"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value val = vm.pop();
        double dval = vm.getNumericValue(val);
        result_series->setCurrent(current_bar, dval);
        return result_series;
    };
    built_in_funcs["count"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value condition_val = vm.pop();

        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto condition_series = std::get<std::shared_ptr<Series>>(condition_val);

        int count = 0;
        for (int i = 0; i < length && current_bar - i >= 0; ++i)
        {
            double val = condition_series->getCurrent(current_bar - i);
            if (!std::isnan(val) && val != 0.0)
            { // Assuming non-zero or non-NaN means true
                count++;
            }
        }
        result_series->setCurrent(current_bar, static_cast<double>(count));
        return result_series;
    };
    built_in_funcs["currbarscount"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        //todo
        return result_series;
    };
    built_in_funcs["dma"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        //含义：求动态移动平均。
        //用法：DMA(X,A)，求X的动态移动平均。
        //算法： 若Y=DMA(X,A)则 Y=A*X+(1-A)*Y'，其中Y'表示上一周期Y值，A必须小于1。
        //例如：DMA(CLOSE,VOL/CAPITAL)表示求以换手率作平滑因子的平均价。
        Value alpha_val = vm.pop();
        Value source_val = vm.pop();

        double alpha = vm.getNumericValue(alpha_val);
        auto source_series = std::get<std::shared_ptr<Series>>(source_val);

        double current_source_val = source_series->getCurrent(current_bar);
        double prev_dma = result_series->getCurrent(current_bar - 1);

        double dma_val;
        if (std::isnan(current_source_val))
        {
            dma_val = NAN;
        }
        else if (std::isnan(prev_dma))
        {
            // DMA的第一个值通常是NaN，或者用当前值播种
            dma_val = current_source_val;
        }
        else
        {
            // DMA计算公式: Y = A * X + (1 - A) * Y'
            dma_val = alpha * current_source_val + (1.0 - alpha) * prev_dma;
        }
        result_series->setCurrent(current_bar, dma_val);
        return result_series;
    };
    built_in_funcs["expma"] = built_in_funcs["ema"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto source_series = std::get<std::shared_ptr<Series>>(source_val);
        // 函数说明：	返回指数移动平均
        // 函数用法：	EMA(X,N)：X的N日指数移动平均.算法:Y=(X*2+Y'*(N-1))/(N+1)
        // 返回指数移动平均:EMA(X,N);
        // 用法:
        // EMA(X,N):X的N日指数移动平均.算法:Y=(X*2+Y'*(N-1))/(N+1)
        // EMA(X,N)相当于SMA(X,N+1,2),N支持变量
        double current_source_val = source_series->getCurrent(current_bar);
        double prev_ema = result_series->getCurrent(current_bar - 1);

        double ema_val;
        if (std::isnan(current_source_val))
        {
            ema_val = NAN;
        }
        else if (std::isnan(prev_ema))
        {
            // EMA的第一个值通常是NaN，或者用当前值播种
            // 这里我们选择在数据不足时返回NaN，直到有足够数据计算
            ema_val = current_source_val;
        }
        else
        {
            // EMA计算公式: Y=(X*2+Y'*(N-1))/(N+1)
            ema_val = (current_source_val * 2 + prev_ema * (length - 1)) / (length + 1);
        }
        result_series->setCurrent(current_bar, ema_val);
        return result_series;
    };
    built_in_funcs["expmema"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto source_series = std::get<std::shared_ptr<Series>>(source_val);

        // EXPMEMA 指数平滑移动平均
        // 返回指数平滑移动平均
        // 用法:
        // EXPMEMA(X,N):X的N日指数平滑移动平均,N不支持变量
        // EXPMEMA同EMA(EXPMA)的差别在于周期不满足N时,返回无效值
        double expmema_val;
        if (current_bar < length - 1)
        {
            expmema_val = NAN; // 数据不足，返回无效值
        }
        else
        {
            double current_source_val = source_series->getCurrent(current_bar);
            double prev_expmema = result_series->getCurrent(current_bar - 1);

            if (std::isnan(current_source_val))
            {
                expmema_val = NAN;
            }
            else if (std::isnan(prev_expmema))
            {
                // 对于第一个有效值，通常用SMA播种
                double sum = 0.0;
                int count = 0;
                for (int i = 0; i < length; ++i)
                {
                    double val = source_series->getCurrent(current_bar - i);
                    if (!std::isnan(val))
                    {
                        sum += val;
                        count++;
                    }
                }
                expmema_val = (count == length) ? sum / count : NAN;
            }
            else
            {
                // EXPMEMA计算公式: Y=(X*2+Y'*(N-1))/(N+1)
                expmema_val = (current_source_val * 2 + prev_expmema * (length - 1)) / (length + 1);
            }
        }
        result_series->setCurrent(current_bar, expmema_val);

        return result_series;
    };
    built_in_funcs["filter"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value condition_val = vm.pop();

        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto condition_series = std::get<std::shared_ptr<Series>>(condition_val);
        // filter(COND, N) 返回 COND 在 N 周期内是否至少有一次为真。
        // 如果 COND 在 N 周期内（包括当前周期）至少有一次为真，则返回 0.0，否则返回 1.0。
        bool any_true = false;
        for (int i = 1; i < length && current_bar - i >= 0; ++i)
        {
            double val = result_series->getCurrent(current_bar - i);
            if (!std::isnan(val) && val != 0.0)
            {
                any_true = true;
                break;
            }
        }
        if(any_true)
            result_series->setCurrent(current_bar, static_cast<double>(false));
        else
            result_series->setCurrent(current_bar, condition_series->getCurrent(current_bar));
        return result_series;
    };
    built_in_funcs["findhigh"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        // FINDHIGH 寻找指定周期内的特定最大值
        // N周期前的M周期内的第T个最大值.
        // 用法:
        // FINDHIGH(VAR,N,M,T):VAR在N日前的M天内第T个最高价
        Value t_val = vm.pop();
        Value m_val = vm.pop();
        Value n_val = vm.pop();
        Value var_val = vm.pop();

        int N = static_cast<int>(vm.getNumericValue(n_val)); // N周期前
        int M = static_cast<int>(vm.getNumericValue(m_val)); // M周期内
        int T = static_cast<int>(vm.getNumericValue(t_val)); // 第T个

        auto var_series = std::get<std::shared_ptr<Series>>(var_val);

        // 计算起始和结束索引
        int start_idx = current_bar - N - M + 1;
        int end_idx = current_bar - N;

        if (start_idx < 0)
        {
            start_idx = 0;
        }

        std::vector<double> values_in_range;
        for (int i = start_idx; i <= end_idx; ++i)
        {
            if (i >= 0 && i < var_series->data.size())
            {
                double val = var_series->getCurrent(i);
                if (!std::isnan(val))
                {
                    values_in_range.push_back(val);
                }
            }
        }

        if (values_in_range.empty() || T <= 0 || T > values_in_range.size())
        {
            result_series->setCurrent(current_bar, NAN);
        }
        else
        {
            // 降序排序以找到第T个最大值
            std::sort(values_in_range.rbegin(), values_in_range.rend());
            result_series->setCurrent(current_bar, values_in_range[T - 1]);
        }
        return result_series;
    };
    built_in_funcs["findhighbars"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        // FINDHIGHBARS 寻找指定周期内的特定最大值到当前的周期数
        // N周期前的M周期内的第T个最大值到当前周期的周期数.
        // 用法:
        // FINDHIGHBARS(VAR,N,M,T):VAR在N日前的M天内第T个最高价到当前周期的周期数
        Value t_val = vm.pop();
        Value m_val = vm.pop();
        Value n_val = vm.pop();
        Value var_val = vm.pop();

        int N = static_cast<int>(vm.getNumericValue(n_val)); // N周期前
        int M = static_cast<int>(vm.getNumericValue(m_val)); // M周期内
        int T = static_cast<int>(vm.getNumericValue(t_val)); // 第T个

        auto var_series = std::get<std::shared_ptr<Series>>(var_val);

        // 计算起始和结束索引
        int start_idx = current_bar - N - M + 1;
        int end_idx = current_bar - N;

        if (start_idx < 0)
        {
            start_idx = 0;
        }

        // 存储 (值, 索引) 对
        std::vector<std::pair<double, int>> values_with_indices;
        for (int i = start_idx; i <= end_idx; ++i)
        {
            if (i >= 0 && i < var_series->data.size())
            {
                double val = var_series->getCurrent(i);
                if (!std::isnan(val))
                {
                    values_with_indices.push_back({val, i});
                }
            }
        }
        if (values_with_indices.empty() || T <= 0 || T > values_with_indices.size())
        {
            result_series->setCurrent(current_bar, NAN);
        }
        else
        {
            // 降序排序以找到第T个最大值，如果值相同，保留原始索引靠前的（即更早的K线）
            std::sort(values_with_indices.begin(), values_with_indices.end(),
                        [](const std::pair<double, int> &a, const std::pair<double, int> &b)
                        {
                            if (a.first != b.first)
                            {
                                return a.first > b.first; // 值大的排前面
                            }
                            return a.second < b.second; // 值相同，索引小的排前面（更早的K线）
                        });

            // 获取第T个最大值的原始索引
            int target_original_idx = values_with_indices[T - 1].second;

            // 计算到当前K线的周期数
            result_series->setCurrent(current_bar, static_cast<double>(current_bar - target_original_idx));
        }
        return result_series;
    };
    built_in_funcs["findlow"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();

        // FINDLOW 寻找指定周期内的特定最小值
        // N周期前的M周期内的第T个最小值.
        // 用法:
        // FINDLOW(VAR,N,M,T):VAR在N日前的M天内第T个最低价
        Value t_val = vm.pop();
        Value m_val = vm.pop();
        Value n_val = vm.pop();
        Value var_val = vm.pop();

        int N = static_cast<int>(vm.getNumericValue(n_val)); // N周期前
        int M = static_cast<int>(vm.getNumericValue(m_val)); // M周期内
        int T = static_cast<int>(vm.getNumericValue(t_val)); // 第T个

        auto var_series = std::get<std::shared_ptr<Series>>(var_val);

        // 计算起始和结束索引
        int start_idx = current_bar - N - M + 1;
        int end_idx = current_bar - N;

        if (start_idx < 0)
        {
            start_idx = 0;
        }

        std::vector<double> values_in_range;
        for (int i = start_idx; i <= end_idx; ++i)
        {
            if (i >= 0 && i < var_series->data.size())
            {
                double val = var_series->getCurrent(i);
                if (!std::isnan(val))
                {
                    values_in_range.push_back(val);
                }
            }
        }

        if (values_in_range.empty() || T <= 0 || T > values_in_range.size())
        {
            result_series->setCurrent(current_bar, NAN);
        }
        else
        {
            // 升序排序以找到第T个最小值
            std::sort(values_in_range.begin(), values_in_range.end());
            result_series->setCurrent(current_bar, values_in_range[T - 1]);
        }
        return result_series;
    };
    built_in_funcs["findlowbars"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        // FINDLOWBARS 寻找指定周期内的特定最小值到当前的周期数
        // N周期前的M周期内的第T个最小值到当前周期的周期数.
        // 用法:
        // FINDLOWBARS(VAR,N,M,T):VAR在N日前的M天内第T个最低价到当前周期的周期数
        Value t_val = vm.pop();
        Value m_val = vm.pop();
        Value n_val = vm.pop();
        Value var_val = vm.pop();

        int N = static_cast<int>(vm.getNumericValue(n_val)); // N周期前
        int M = static_cast<int>(vm.getNumericValue(m_val)); // M周期内
        int T = static_cast<int>(vm.getNumericValue(t_val)); // 第T个

        auto var_series = std::get<std::shared_ptr<Series>>(var_val);

        // 计算起始和结束索引
        int start_idx = current_bar - N - M + 1;
        int end_idx = current_bar - N;

        if (start_idx < 0)
        {
            start_idx = 0;
        }

        // 存储 (值, 索引) 对
        std::vector<std::pair<double, int>> values_with_indices;
        for (int i = start_idx; i <= end_idx; ++i)
        {
            if (i >= 0 && i < var_series->data.size())
            {
                double val = var_series->getCurrent(i);
                if (!std::isnan(val))
                {
                    values_with_indices.push_back({val, i});
                }
            }
        }
        if (values_with_indices.empty() || T <= 0 || T > values_with_indices.size())
        {
            result_series->setCurrent(current_bar, NAN);
        }
        else
        {
            // 升序排序以找到第T个最小值，如果值相同，保留原始索引靠前的（即更早的K线）
            std::sort(values_with_indices.begin(), values_with_indices.end(),
                        [](const std::pair<double, int> &a, const std::pair<double, int> &b)
                        {
                            if (a.first != b.first)
                            {
                                return a.first < b.first; // 值小的排前面
                            }
                            return a.second < b.second; // 值相同，索引小的排前面（更早的K线）
                        });

            // 获取第T个最小值的原始索引
            int target_original_idx = values_with_indices[T - 1].second;

            // 计算到当前K线的周期数
            result_series->setCurrent(current_bar, static_cast<double>(current_bar - target_original_idx));
        }

        return result_series;
    };
    built_in_funcs["hhv"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        auto *p = std::get_if<std::shared_ptr<Series>>(&source_val);
        if (!p)
        {
            return result_series;
        }
        auto source_series = *p;
        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));

        double highest_val = NAN;
        bool first = true;
        for (int i = 0; i < length && current_bar - i >= 0; ++i)
        {
            double val = source_series->getCurrent(current_bar - i);
            if (!std::isnan(val))
            {
                if (first)
                {
                    highest_val = val;
                    first = false;
                }
                else
                {
                    highest_val = std::max(highest_val, val);
                }
            }
        }
        result_series->setCurrent(current_bar, highest_val);
        return result_series;
    };
    built_in_funcs["hhvbars"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        auto *p = std::get_if<std::shared_ptr<Series>>(&source_val);
        if (!p)
        {
            return result_series;
        }
        auto source_series = *p;
        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));

        double highest_val = NAN;
        int highest_idx = -1;
        bool first = true;

        for (int i = 0; i < length && current_bar - i >= 0; ++i)
        {
            double val = source_series->getCurrent(current_bar - i);
            if (!std::isnan(val))
            {
                if (first)
                {
                    highest_val = val;
                    highest_idx = i;
                    first = false;
                }
                else
                {
                    if (val >= highest_val)
                    { // 注意：如果值相等，Hithink/TDX通常返回更近的那个bar
                        highest_val = val;
                        highest_idx = i;
                    }
                }
            }
        }
        result_series->setCurrent(current_bar, static_cast<double>(highest_idx));
        return result_series;
    };
    built_in_funcs["hod"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value offset_val = vm.pop();
        Value source_val = vm.pop();

        auto *p = std::get_if<std::shared_ptr<Series>>(&source_val);
        if (!p)
        {
            return result_series;
        }
        auto source_series = *p;
        int current_bar = vm.getCurrentBarIndex();
        int offset = static_cast<int>(vm.getNumericValue(offset_val));

        double hod_val = source_series->getCurrent(current_bar - offset);
        result_series->setCurrent(current_bar, hod_val);
        return result_series;
    };
    built_in_funcs["islastbar"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        int total_bars = vm.getTotalBars();

        result_series->setCurrent(current_bar, static_cast<double>(current_bar == total_bars - 1));
        return result_series;
    };
    built_in_funcs["llv"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        auto *p = std::get_if<std::shared_ptr<Series>>(&source_val);
        if (!p)
        {
            return result_series;
        }
        auto source_series = *p;
        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));

        double lowest_val = NAN;
        bool first = true;
        for (int i = 0; i < length && current_bar - i >= 0; ++i)
        {
            double val = source_series->getCurrent(current_bar - i);
            if (!std::isnan(val))
            {
                if (first)
                {
                    lowest_val = val;
                    first = false;
                }
                else
                {
                    lowest_val = std::min(lowest_val, val);
                }
            }
        }
        result_series->setCurrent(current_bar, lowest_val);
        return result_series;
    };
    built_in_funcs["llvbars"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        auto *p = std::get_if<std::shared_ptr<Series>>(&source_val);
        if (!p)
        {
            return result_series;
        }
        auto source_series = *p;
        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));

        double lowest_val = NAN;
        int lowest_idx = -1;
        bool first = true;

        for (int i = 0; i < length && current_bar - i >= 0; ++i)
        {
            double val = source_series->getCurrent(current_bar - i);
            if (!std::isnan(val))
            {
                if (first)
                {
                    lowest_val = val;
                    lowest_idx = i;
                    first = false;
                }
                else
                {
                    if (val <= lowest_val)
                    { // 注意：如果值相等，Hithink/TDX通常返回更近的那个bar
                        lowest_val = val;
                        lowest_idx = i;
                    }
                }
            }
        }
        result_series->setCurrent(current_bar, static_cast<double>(lowest_idx));
        return result_series;
    };
    built_in_funcs["lod"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value offset_val = vm.pop();
        Value source_val = vm.pop();

        auto *p = std::get_if<std::shared_ptr<Series>>(&source_val);
        if (!p)
        {
            return result_series;
        }
        auto source_series = *p;
        int current_bar = vm.getCurrentBarIndex();
        int offset = static_cast<int>(vm.getNumericValue(offset_val));

        double lod_val = source_series->getCurrent(current_bar - offset);
        result_series->setCurrent(current_bar, lod_val);
        return result_series;
    };
    built_in_funcs["lowrange"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value offset_val = vm.pop();
        Value source_val = vm.pop();

        auto *p = std::get_if<std::shared_ptr<Series>>(&source_val);
        if (!p)
        {
            return result_series;
        }
        auto source_series = *p;
        int current_bar = vm.getCurrentBarIndex();
        int offset = static_cast<int>(vm.getNumericValue(offset_val));

        double low_val = source_series->getCurrent(current_bar - offset);
        result_series->setCurrent(current_bar, low_val);
        return result_series;
    };
    built_in_funcs["ma"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));

        auto *p = std::get_if<std::shared_ptr<Series>>(&source_val);
        if (p)
        {
            auto source_series = *p;

            double sum = 0.0;
            int count = 0;
            for (int i = 0; i < length && current_bar - i >= 0; ++i)
            {
                double val = source_series->getCurrent(current_bar - i);
                if (!std::isnan(val))
                {
                    sum += val;
                    count++;
                }
            }

            double ma_val = (count == length) ? sum / count : NAN;
            result_series->setCurrent(current_bar, ma_val);
        }
        return result_series;
    };
    built_in_funcs["mema"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto source_series = std::get<std::shared_ptr<Series>>(source_val);

        double current_source_val = source_series->getCurrent(current_bar);
        double prev_mema = result_series->getCurrent(current_bar - 1);

        double mema_val;
        if (std::isnan(current_source_val))
        {
            mema_val = NAN;
        }
        else if (std::isnan(prev_mema))
        {
            // MEMA的第一个值通常是NaN，或者用SMA播种
            // 这里我们选择在数据不足时返回NaN，直到有足够数据计算
            mema_val = NAN;
        }
        else
        {
            // MEMA计算公式: MEMA = (当前值 + 前一日MEMA * (周期 - 1)) / 周期
            mema_val = (current_source_val + prev_mema * (length - 1)) / length;
        }
        result_series->setCurrent(current_bar, mema_val);
        return result_series;
    };
    built_in_funcs["mular"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        // MULAR 累乘
        // 求累乘.
        // 用法:
        // MULAR(X,N),统计N周期中X的乘积.N=0则从第一个有效值开始.
        // 例如:
        // MULAR(C/REF(C,1),0)表示统计从上市第一天以来的复利
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto source_series = std::get<std::shared_ptr<Series>>(source_val);
        double product = 1.0;
        bool has_nan = false;

        if (length == 0)
        { // N=0则从第一个有效值开始
            for (int i = 0; i <= current_bar; ++i)
            {
                double val = source_series->getCurrent(i);
                if (std::isnan(val))
                {
                    has_nan = true;
                    break;
                }
                product *= val;
            }
        }
        else
        { // 统计N周期中X的乘积
            for (int i = 0; i < length; ++i)
            {
                if (current_bar - i < 0)
                {
                    has_nan = true; // Not enough data for the full length
                    break;
                }
                double val = source_series->getCurrent(current_bar - i);
                if (std::isnan(val))
                {
                    has_nan = true;
                    break;
                }
                product *= val;
            }
        }

        if (has_nan)
        {
            result_series->setCurrent(current_bar, NAN);
        }
        else
        {
            result_series->setCurrent(current_bar, product);
        }
        
        return result_series;
    };
    built_in_funcs["range"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        // RANGE 介于某个范围之间
        // RANGE(A,B,C):A在B和C范围之间,B<A<C.
        // 用法:
        // RANGE(A,B,C)表示A大于B同时小于C时返回1,否则返回0
        Value c_val = vm.pop();
        Value b_val = vm.pop();
        Value a_val = vm.pop();

        double A = vm.getNumericValue(a_val);
        double B = vm.getNumericValue(b_val);
        double C = vm.getNumericValue(c_val);

        // RANGE(A,B,C)表示A大于B同时小于C时返回1,否则返回0
        double range_val = (A > B && A < C) ? 1.0 : 0.0;
        result_series->setCurrent(current_bar, range_val);
        return result_series;
    };
    built_in_funcs["ref"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value offset_val = vm.pop();
        Value source_val = vm.pop();

        auto *p = std::get_if<std::shared_ptr<Series>>(&source_val);
        if (!p)
        {
            return result_series;
        }
        auto source_series = *p;
        int current_bar = vm.getCurrentBarIndex();
        int offset = static_cast<int>(vm.getNumericValue(offset_val));

        double ref_val = source_series->getCurrent(current_bar - offset);
        result_series->setCurrent(current_bar, ref_val);
        return result_series;
    };
    built_in_funcs["refdate"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value offset_val = vm.pop();
        Value source_val = vm.pop();

        auto *p = std::get_if<std::shared_ptr<Series>>(&source_val);
        if (!p)
        {
            return result_series;
        }
        auto source_series = *p;
        int current_bar = vm.getCurrentBarIndex();
        int offset = static_cast<int>(vm.getNumericValue(offset_val));

        // refdate(X, N) 返回 X 在 N 周期前的日期。
        // 这里我们假设 X 是一个 Series，并且我们只是返回其在 (current_bar - offset) 处的日期。
        // 由于 Series 存储的是 double，我们可能需要一个机制来存储和检索日期。
        // 目前，我们只能返回一个 double 值，所以这里返回 NaN 或一个占位符。
        // 实际实现需要访问K线数据中的日期信息。
        result_series->setCurrent(current_bar, NAN); // Placeholder
        return result_series;
    };
    built_in_funcs["refv"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value offset_val = vm.pop();
        Value source_val = vm.pop();

        auto *p = std::get_if<std::shared_ptr<Series>>(&source_val);
        if (!p)
        {
            return result_series;
        }
        auto source_series = *p;
        int current_bar = vm.getCurrentBarIndex();
        int offset = static_cast<int>(vm.getNumericValue(offset_val));

        double ref_val = source_series->getCurrent(current_bar - offset);
        result_series->setCurrent(current_bar, ref_val);
        return result_series;
    };
    built_in_funcs["reverse"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value source_val = vm.pop();
        
        int current_bar = vm.getCurrentBarIndex();
        auto source_series = std::get<std::shared_ptr<Series>>(source_val);

        // reverse(X) 返回 X 的反向值。
        // 这通常意味着如果 X 是一个 Series，则返回其在当前K线上的值。
        // 如果 X 是一个数字，则返回该数字。
        // 在这里，我们假设 reverse 只是返回 Series 在当前K线的值。
        // 如果有更复杂的反向逻辑（例如反转整个序列），则需要更多上下文。
        double reversed_val = source_series->getCurrent(current_bar);
        result_series->setCurrent(current_bar, reversed_val);
        return result_series;
    };
    built_in_funcs["sma"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        // Hithink/TDX SMA函数有三个参数: SMA(X,N,M)
        // X: 源数据, N: 周期, M: 权重 (通常为1, 表示简单移动平均)
        Value weight_val = vm.pop(); // M
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto source_series = std::get<std::shared_ptr<Series>>(source_val);

        double sum = 0.0;
        int count = 0;
        for (int i = 0; i < length && current_bar - i >= 0; ++i)
        {
            double val = source_series->getCurrent(current_bar - i);
            if (!std::isnan(val))
            {
                sum += val;
                count++;
            }
        }

        double sma_val = (count == length) ? sum / count : NAN;
        result_series->setCurrent(current_bar, sma_val);
        return result_series;
    };
    built_in_funcs["sum"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto source_series = std::get<std::shared_ptr<Series>>(source_val);

        double sum = 0.0;
        int count = 0;
        for (int i = 0; i < length && current_bar - i >= 0; ++i)
        {
            double val = source_series->getCurrent(current_bar - i);
            if (!std::isnan(val))
            {
                sum += val;
                count++;
            }
        }
        double sum_val = (count == length) ? sum : NAN;
        result_series->setCurrent(current_bar, sum_val);
        return result_series;
    };
    built_in_funcs["sumbars"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto source_series = std::get<std::shared_ptr<Series>>(source_val);

        double sum = 0.0;
        int count = 0;
        for (int i = 0; i < length && current_bar - i >= 0; ++i)
        {
            double val = source_series->getCurrent(current_bar - i);
            if (!std::isnan(val))
            {
                sum += val;
                count++;
            }
        }
        double sum_val = (count == length) ? sum : NAN;
        result_series->setCurrent(current_bar, sum_val);
        return result_series;
    };
    built_in_funcs["tfilt"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value condition_val = vm.pop();

        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto condition_series = std::get<std::shared_ptr<Series>>(condition_val);

        // tfilt(COND, N) 返回 COND 在 N 周期内是否一直为真。
        // 如果 COND 在 N 周期内（包括当前周期）都为真，则返回 1.0，否则返回 0.0。
        bool all_true = true;
        for (int i = 0; i < length && current_bar - i >= 0; ++i)
        {
            double val = condition_series->getCurrent(current_bar - i);
            if (std::isnan(val) || val == 0.0)
            {
                all_true = false;
                break;
            }
        }
        result_series->setCurrent(current_bar, static_cast<double>(all_true));
        return result_series;
    };
    built_in_funcs["tfilter"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value condition_val = vm.pop();

        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto condition_series = std::get<std::shared_ptr<Series>>(condition_val);

        // tfilter(COND, N) 返回 COND 在 N 周期内是否至少有一次为真。
        // 如果 COND 在 N 周期内（包括当前周期）至少有一次为真，则返回 1.0，否则返回 0.0。
        bool any_true = false;
        for (int i = 0; i < length && current_bar - i >= 0; ++i)
        {
            double val = condition_series->getCurrent(current_bar - i);
            if (!std::isnan(val) && val != 0.0)
            {
                any_true = true;
                break;
            }
        }
        result_series->setCurrent(current_bar, static_cast<double>(any_true));
        return result_series;
    };
    built_in_funcs["tma"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto source_series = std::get<std::shared_ptr<Series>>(source_val);

        double tma_val;
        if (current_bar < length - 1)
        {
            tma_val = NAN; // Not enough data for the first TMA calculation
        }
        else
        {
            // TMA is the SMA of an SMA
            // First, calculate the SMA of the source series
            double sum_sma = 0.0;
            for (int i = 0; i < length; ++i)
            {
                double inner_sum = 0.0;
                int inner_count = 0;
                for (int j = 0; j < length && (current_bar - i - j) >= 0; ++j)
                {
                    double val = source_series->getCurrent(current_bar - i - j);
                    if (!std::isnan(val))
                    {
                        inner_sum += val;
                        inner_count++;
                    }
                }
                if (inner_count == length)
                {
                    sum_sma += (inner_sum / length);
                }
                else
                {
                    sum_sma = NAN; // If any inner SMA is NaN, the whole TMA is NaN
                    break;
                }
            }

            if (!std::isnan(sum_sma))
            {
                tma_val = sum_sma / length;
            }
            else
            {
                tma_val = NAN;
            }
        }
        result_series->setCurrent(current_bar, tma_val);
        return result_series;
    };
    built_in_funcs["totalrange"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        //todo
        return result_series;
    };
    built_in_funcs["totalbarscount"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        int total_bars = vm.getTotalBars();

        result_series->setCurrent(current_bar, static_cast<double>(total_bars));
        return result_series;
    };
    built_in_funcs["wma"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto source_series = std::get<std::shared_ptr<Series>>(source_val);

        double wma_val = NAN;
        if (current_bar >= length - 1)
        {
            double sum_weighted_values = 0.0;
            double sum_weights = 0.0;
            for (int i = 0; i < length; ++i)
            {
                double val = source_series->getCurrent(current_bar - i);
                double weight = length - i; // Weights are length, length-1, ..., 1
                if (!std::isnan(val))
                {
                    sum_weighted_values += val * weight;
                    sum_weights += weight;
                }
                else
                {
                    // If any value in the window is NaN, the result is NaN
                    sum_weighted_values = NAN;
                    break;
                }
            }
            if (!std::isnan(sum_weighted_values) && sum_weights > 0)
            {
                wma_val = sum_weighted_values / sum_weights;
            }
        }
        result_series->setCurrent(current_bar, wma_val);
        return result_series;
    };
    built_in_funcs["xma"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto source_series = std::get<std::shared_ptr<Series>>(source_val);

        double current_source_val = source_series->getCurrent(current_bar);
        double prev_xma = result_series->getCurrent(current_bar - 1);

        double xma_val;
        if (std::isnan(current_source_val))
        {
            xma_val = NAN;
        }
        else if (std::isnan(prev_xma))
        {
            // XMA的第一个值通常是NaN，或者用SMA播种
            // 这里我们选择在数据不足时返回NaN，直到有足够数据计算
            xma_val = NAN;
        }
        else
        {
            // XMA计算公式: XMA = (当前值 + 前一日XMA * (周期 - 1)) / 周期
            xma_val = (current_source_val + prev_xma * (length - 1)) / length;
        }
        result_series->setCurrent(current_bar, xma_val);
        return result_series;
    };

    //形态函数
    built_in_funcs["cost"] = [](PineVM &vm) -> Value
    {
        //todo
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        // COST函数在PineScript中通常用于获取某个价格序列的成本价，
        // 这通常与持仓成本或平均成本相关。
        // 在没有交易数据的情况下，我们假设它返回当前K线的收盘价，
        // 或者如果提供了参数，则返回该参数的值。
        // 鉴于Hithink/TDX的COST函数通常返回一个Series，我们这里返回一个Series。

        // 如果栈顶只有一个result_series，说明没有额外的参数，
        // 此时COST可能默认返回close。
        // 但根据Hithink/TDX的用法，COST(X)返回X的成本价，
        // 如果没有X，则返回当前股票的平均成本。
        // 在我们的模拟环境中，我们简化为返回当前K线的收盘价。

        int current_bar = vm.getCurrentBarIndex();
        // 假设我们有一个内置的 'close' Series
        if (vm.built_in_vars.count("close"))
        {
            auto close_series = std::get<std::shared_ptr<Series>>(vm.built_in_vars.at("close"));
            if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar]))
            {
                result_series->setCurrent(current_bar, close_series->getCurrent(current_bar));
            }
        }
        else
        {
            // 如果没有close数据，返回NaN
            if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar]))
            {
                result_series->setCurrent(current_bar, NAN);
            }
        }
        return result_series;
    };
    built_in_funcs["costex"] = [](PineVM &vm) -> Value
    {
        //todo
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        return result_series;
    };
    built_in_funcs["lfs"] = [](PineVM &vm) -> Value
    {
        //todo
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        return result_series;
    };
    built_in_funcs["lwinner"] = [](PineVM &vm) -> Value
    {
        //todo
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        return result_series;
    };
    built_in_funcs["newsar"] = [](PineVM &vm) -> Value
    {
        //todo
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        return result_series;
    };
    built_in_funcs["ppart"] = [](PineVM &vm) -> Value
    {
        //todo
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        return result_series;
    };
    built_in_funcs["pwinner"] = [](PineVM &vm) -> Value
    {
        //todo
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        return result_series;
    };
    built_in_funcs["sar"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        //todo
        return result_series;
    };
    built_in_funcs["sarturn"] = [](PineVM &vm) -> Value
    {
        //todo
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        return result_series;
    };
    built_in_funcs["winner"] = [](PineVM &vm) -> Value
    {
        //todo
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        return result_series;
    };

    //数学函数
    built_in_funcs["abs"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value val = vm.pop();
        double dval = vm.getNumericValue(val);
        result_series->setCurrent(current_bar, std::abs(dval));
        return result_series;
    };
    built_in_funcs["acos"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value val = vm.pop();
        double dval = vm.getNumericValue(val);
        result_series->setCurrent(current_bar, std::acos(dval));
        return result_series;
    };
    built_in_funcs["asin"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value val = vm.pop();
        double dval = vm.getNumericValue(val);
        result_series->setCurrent(current_bar, std::asin(dval));
        return result_series;
    };
    built_in_funcs["atan"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value val = vm.pop();
        double dval = vm.getNumericValue(val);
        result_series->setCurrent(current_bar, std::atan(dval));
        return result_series;
    };
    built_in_funcs["between"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value high_val = vm.pop();
        Value low_val = vm.pop();
        Value source_val = vm.pop();

        double source = vm.getNumericValue(source_val);
        double low = vm.getNumericValue(low_val);
        double high = vm.getNumericValue(high_val);

        if (std::isnan(source) || std::isnan(low) || std::isnan(high))
        {
            result_series->setCurrent(current_bar, NAN);
        }
        else
        {
            result_series->setCurrent(current_bar, static_cast<double>(source >= low && source <= high));
        }
        return result_series;
    };
    built_in_funcs["ceil"] = built_in_funcs["ceiling"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value val = vm.pop();
        double dval = vm.getNumericValue(val);
        result_series->setCurrent(current_bar, std::ceil(dval));
        return result_series;
    };
    built_in_funcs["cos"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value val = vm.pop();
        double dval = vm.getNumericValue(val);
        result_series->setCurrent(current_bar, std::cos(dval));
        return result_series;
    };
    built_in_funcs["exp"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value val = vm.pop();
        double dval = vm.getNumericValue(val);
        result_series->setCurrent(current_bar, std::exp(dval));
        return result_series;
    };
    built_in_funcs["floor"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value val = vm.pop();
        double dval = vm.getNumericValue(val);
        if (std::isnan(dval))
        {
            result_series->setCurrent(current_bar, NAN);
        }
        else
        {
            result_series->setCurrent(current_bar, static_cast<double>(std::floor(dval)));
        }
        return result_series;
    };
    built_in_funcs["facepart"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value val = vm.pop();
        double dval = vm.getNumericValue(val);
        // Hithink/TDX的FACEPART函数通常返回一个数的整数部分。
        // 例如，FACEPART(3.14) = 3, FACEPART(-2.7) = -2。
        // 这与 floor() 函数在正数时相同，但在负数时不同 (floor(-2.7) = -3)。
        // 应该使用 trunc() 函数来获取整数部分，它只是截断小数部分。
        if (std::isnan(dval))
        {
            result_series->setCurrent(current_bar, NAN);
        }
        else
        {
            result_series->setCurrent(current_bar, static_cast<double>(std::trunc(dval)));
        }
        return result_series;
    };
    built_in_funcs["intpart"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value val = vm.pop();
        double dval = vm.getNumericValue(val);
        if (std::isnan(dval))
        {
            result_series->setCurrent(current_bar, NAN);
        }
        else
        {
            result_series->setCurrent(current_bar, static_cast<double>(static_cast<long long>(dval)));
        }
        return result_series;
    };
    built_in_funcs["ln"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value val = vm.pop();
        double dval = vm.getNumericValue(val);
        if (std::isnan(dval))
        {
            result_series->setCurrent(current_bar, NAN);
        }
        else
        {
            result_series->setCurrent(current_bar, static_cast<double>(std::log(dval)));
        }
        return result_series;
    };
    built_in_funcs["log"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value val = vm.pop();
        double dval = vm.getNumericValue(val);
        if (std::isnan(dval))
        {
            result_series->setCurrent(current_bar, NAN);
        }
        else
        {
            result_series->setCurrent(current_bar, static_cast<double>(std::log10(dval)));
        }
        return result_series;
    };
    built_in_funcs["max"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value val2 = vm.pop();
        Value val1 = vm.pop();

        double dval1 = vm.getNumericValue(val1);
        double dval2 = vm.getNumericValue(val2);

        if (std::isnan(dval1) || std::isnan(dval2))
        {
            result_series->setCurrent(current_bar, NAN);
        }
        else
        {
            result_series->setCurrent(current_bar, std::max(dval1, dval2));
        }
        return result_series;
    };
    built_in_funcs["min"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value val2 = vm.pop();
        Value val1 = vm.pop();

        double dval1 = vm.getNumericValue(val1);
        double dval2 = vm.getNumericValue(val2);
        if (std::isnan(dval1) || std::isnan(dval2))
        {
            result_series->setCurrent(current_bar, NAN);
        }
        else
        {
            result_series->setCurrent(current_bar, std::min(dval1, dval2));
        }
        return result_series;
    };
    built_in_funcs["mod"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value divisor_val = vm.pop();
        Value dividend_val = vm.pop();

        double dividend = vm.getNumericValue(dividend_val);
        double divisor = vm.getNumericValue(divisor_val);

        if (std::isnan(dividend) || std::isnan(divisor) || divisor == 0.0)
        {
            result_series->setCurrent(current_bar, NAN);
        }
        else
        {
            result_series->setCurrent(current_bar, static_cast<double>(static_cast<long long>(dividend) % static_cast<long long>(divisor)));
        }
        return result_series;
    };
    built_in_funcs["pow"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value exponent_val = vm.pop();
        Value base_val = vm.pop();

        double base = vm.getNumericValue(base_val);
        double exponent = vm.getNumericValue(exponent_val);

        if (std::isnan(base) || std::isnan(exponent))
        {
            result_series->setCurrent(current_bar, NAN);
        }
        else
        {
            result_series->setCurrent(current_bar, std::pow(base, exponent));
        }
        return result_series;
    };
    built_in_funcs["rand"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        // 生成一个0到1之间的随机浮点数
        double random_value = static_cast<double>(rand()) / RAND_MAX;
        result_series->setCurrent(current_bar, random_value);
        return result_series;
    };
    built_in_funcs["round"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value val = vm.pop();
        double dval = vm.getNumericValue(val);
        if (std::isnan(dval))
        {
            result_series->setCurrent(current_bar, NAN);
        }
        else
        {
            result_series->setCurrent(current_bar, static_cast<double>(std::round(dval)));
        }
        return result_series;
    };
    built_in_funcs["round2"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value decimal_places_val = vm.pop();
        Value val = vm.pop();

        double dval = vm.getNumericValue(val);
        int decimal_places = static_cast<int>(vm.getNumericValue(decimal_places_val));

        if (std::isnan(dval))
        {
            result_series->setCurrent(current_bar, NAN);
        }
        else
        {
            double factor = std::pow(10.0, decimal_places);
            result_series->setCurrent(current_bar, std::round(dval * factor) / factor);
        }
        return result_series;
    };
    built_in_funcs["sign"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value val = vm.pop();
        double dval = vm.getNumericValue(val);
        if (std::isnan(dval))
        {
            result_series->setCurrent(current_bar, NAN);
        }
        else if (dval > 0)
        {
            result_series->setCurrent(current_bar, 1.0);
        }
        else if (dval < 0)
        {
            result_series->setCurrent(current_bar, -1.0);
        }
        else
        {
            result_series->setCurrent(current_bar, 0.0);
        }
        return result_series;
    };
    built_in_funcs["sin"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value val = vm.pop();
        double dval = vm.getNumericValue(val);
        result_series->setCurrent(current_bar, std::sin(dval));
        return result_series;
    };
    built_in_funcs["sqrt"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value val = vm.pop();
        double dval = vm.getNumericValue(val);
        if (std::isnan(dval))
        {
            result_series->setCurrent(current_bar, NAN);
        }
        else
        {
            result_series->setCurrent(current_bar, static_cast<double>(std::sqrt(dval)));
        }
        return result_series;
    };
    built_in_funcs["tan"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value val = vm.pop();
        double dval = vm.getNumericValue(val);
        result_series->setCurrent(current_bar, std::tan(dval));
        return result_series;
    };
    //时间函数

    //绘图函数

    //选择函数
    built_in_funcs["if"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value false_val = vm.pop();
        Value true_val = vm.pop();
        Value condition_val = vm.pop();

        bool condition = vm.getBoolValue(condition_val);

        if (condition)
        {
            result_series->setCurrent(current_bar, std::get<double>(true_val));
        }
        else
        {
            result_series->setCurrent(current_bar, std::get<double>(false_val));
        }
        return result_series;
    };
    built_in_funcs["ifc"] = [](PineVM &vm) -> Value
    {
        //todo
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        return result_series;
    };
    built_in_funcs["iff"] = [](PineVM &vm) -> Value
    {
        //todo
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        return result_series;
    };
    built_in_funcs["ifn"] = [](PineVM &vm) -> Value
    {
        //todo
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        return result_series;
    };
    built_in_funcs["testskip"] = [](PineVM &vm) -> Value
    {
        //todo
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        return result_series;
    };
    built_in_funcs["valuewhen"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        // VALUEWHEN 条件跟随
        // VALUEWHEN(COND,X) 
        // 当COND条件成立时,取X的当前值,否则取VALUEWHEN的上个值

        Value source_val = vm.pop();
        Value condition_val = vm.pop();
        int current_bar = vm.getCurrentBarIndex();
        int offset = 1;

        double result_val = NAN;
        if (current_bar == 0)
        {
            // 第一个bar，如果条件满足，则取X的值，否则为NaN
            if (vm.getBoolValue(condition_val))
            {
                result_val = vm.getNumericValue(source_val);
            }
            else
            {
                result_val = NAN;
            }
        }
        else
        {
            // 从前一个bar的结果中获取
            double prev_result = result_series->getCurrent(current_bar - 1);

            if (vm.getBoolValue(condition_val))
            {
                result_val = vm.getNumericValue(source_val);
            }
            else
            {
                result_val = prev_result;
            }
        }
        result_series->setCurrent(current_bar, result_val);
        return result_series;
    };
    //统计函数
    built_in_funcs["avedev"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        // AVEDEV 平均绝对偏差
        // AVEDEV(X,N) 返回平均绝对偏差

        Value length_val = vm.pop();
        Value source_val = vm.pop();

        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto source_series = std::get<std::shared_ptr<Series>>(source_val);

        double sum = 0.0;
        int count = 0;
        for (int i = 0; i < length && current_bar - i >= 0; ++i)
        {
            double val = source_series->getCurrent(current_bar - i);
            if (!std::isnan(val))
            {
                sum += val;
                count++;
            }
        }

        double avg = (count > 0) ? sum / count : NAN;
        double sum_abs_dev = 0.0;
        int dev_count = 0;

        if (!std::isnan(avg))
        {
            for (int i = 0; i < length && current_bar - i >= 0; ++i)
            {
                double val = source_series->getCurrent(current_bar - i);
                if (!std::isnan(val))
                {
                    sum_abs_dev += std::abs(val - avg);
                    dev_count++;
                }
            }
        }

        double avedev_val = (dev_count > 0) ? sum_abs_dev / dev_count : NAN;
        result_series->setCurrent(current_bar, avedev_val);
       return result_series;
    };
    built_in_funcs["beta"] = [](PineVM &vm) -> Value
    {
        //todo
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        return result_series;
    };
    built_in_funcs["betax"] = [](PineVM &vm) -> Value
    {
        //todo
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        return result_series;
    };
    built_in_funcs["covar"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        // COVAR 协方差
        // COVAR(X,Y,N) 返回X和Y的N周期的协方差

        Value length_val = vm.pop();
        Value source2_val = vm.pop();
        Value source1_val = vm.pop();

        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto source1_series = std::get<std::shared_ptr<Series>>(source1_val);
        auto source2_series = std::get<std::shared_ptr<Series>>(source2_val);

        double sum_x = 0.0;
        double sum_y = 0.0;
        double sum_xy = 0.0;
        int count = 0;

        for (int i = 0; i < length && current_bar - i >= 0; ++i)
        {
            double x = source1_series->getCurrent(current_bar - i);
            double y = source2_series->getCurrent(current_bar - i);

            if (!std::isnan(x) && !std::isnan(y))
            {
                sum_x += x;
                sum_y += y;
                sum_xy += (x * y);
                count++;
            }
        }

        double covar_val = NAN;
        if (count == length && count > 1)
        {
            double mean_x = sum_x / count;
            double mean_y = sum_y / count;
            covar_val = (sum_xy - count * mean_x * mean_y) / (count - 1);
        }
        result_series->setCurrent(current_bar, covar_val);
        return result_series;
    };
    built_in_funcs["devsq"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto source_series = std::get<std::shared_ptr<Series>>(source_val);

        double sum = 0.0;
        int count = 0;
        for (int i = 0; i < length && current_bar - i >= 0; ++i)
        {
            double val = source_series->getCurrent(current_bar - i);
            if (!std::isnan(val))
            {
                sum += val;
                count++;
            }
        }

        double avg = (count > 0) ? sum / count : NAN;
        double sum_sq_dev = 0.0;
        int dev_count = 0;

        if (!std::isnan(avg))
        {
            for (int i = 0; i < length && current_bar - i >= 0; ++i)
            {
                double val = source_series->getCurrent(current_bar - i);
                if (!std::isnan(val))
                {
                    sum_sq_dev += std::pow(val - avg, 2);
                    dev_count++;
                }
            }
        }

        double devsq_val = (dev_count > 0) ? sum_sq_dev : NAN; // DevSq is sum of squared deviations, not average
        result_series->setCurrent(current_bar, devsq_val);
        return result_series;
    };
    built_in_funcs["forcast"] = [](PineVM &vm) -> Value
    {
        //todo
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        return result_series;
    };
    built_in_funcs["relate"] = [](PineVM &vm) -> Value
    {
        //todo
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        return result_series;
    };
    built_in_funcs["slope"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        auto *p = std::get_if<std::shared_ptr<Series>>(&source_val);
        if (!p)
        {
            return result_series;
        }
        auto source_series = *p;
        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));
 
        if (current_bar < length - 1)
        { // 数据不足
            result_series->setCurrent(current_bar, NAN);
        }
        else
        {
            // 计算 x 的平均值 (bar_index - length + 1 到 bar_index)
            double sum_x = 0.0;
            for (int i = 0; i < length; ++i)
            {
                sum_x += (current_bar - (length - 1) + i);
            }
            double avg_x = sum_x / length;

            // 计算 y 的平均值
            double sum_y = 0.0;
            for (int i = 0; i < length; ++i)
            {
                sum_y += source_series->getCurrent(current_bar - (length - 1) + i);
            }
            double avg_y = sum_y / length;

            double numerator = 0.0;   // 分子
            double denominator = 0.0; // 分母

            for (int i = 0; i < length; ++i)
            {
                double x_val = (current_bar - (length - 1) + i);
                double y_val = source_series->getCurrent(current_bar - (length - 1) + i);

                if (std::isnan(y_val))
                { // 如果有NaN值，则结果为NaN
                    numerator = NAN;
                    denominator = NAN;
                    break;
                }
                numerator += (x_val - avg_x) * (y_val - avg_y);
                denominator += (x_val - avg_x) * (x_val - avg_x);
            }

            if (std::isnan(numerator) || std::isnan(denominator) || denominator == 0.0)
            {
                result_series->setCurrent(current_bar, NAN);
            }
            else
            {
                result_series->setCurrent(current_bar, numerator / denominator);
            }
        }
        return result_series;
    };
    built_in_funcs["std"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        auto *p = std::get_if<std::shared_ptr<Series>>(&source_val);
        if (!p)
        {
            return result_series;
        }
        auto source_series = *p;
        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));

        if (current_bar < length - 1)
        { // 数据不足
            result_series->setCurrent(current_bar, NAN);
        }
        else
        {
            double sum = 0.0;
            int count = 0;
            for (int i = 0; i < length; ++i)
            {
                double val = source_series->getCurrent(current_bar - i);
                if (!std::isnan(val))
                {
                    sum += val;
                    count++;
                }
            }

            if (count == 0)
            {
                result_series->setCurrent(current_bar, NAN);
            }
            else
            {
                double mean = sum / count;
                double sum_sq_diff = 0.0;
                for (int i = 0; i < length; ++i)
                {
                    double val = source_series->getCurrent(current_bar - i);
                    if (!std::isnan(val))
                    {
                        sum_sq_diff += (val - mean) * (val - mean);
                    }
                }
                // 使用样本标准差 (n-1)
                double std_dev = (count > 1) ? std::sqrt(sum_sq_diff / (count - 1)) : 0.0;
                result_series->setCurrent(current_bar, std_dev);
            }
        }
        return result_series;
    };
    built_in_funcs["stddev"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        auto *p = std::get_if<std::shared_ptr<Series>>(&source_val);
        if (!p)
        {
            return result_series;
        }
        auto source_series = *p;
        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));

        if (current_bar < length - 1)
        { // 数据不足
            result_series->setCurrent(current_bar, NAN);
        }
        else
        {
            double sum = 0.0;
            int count = 0;
            for (int i = 0; i < length; ++i)
            {
                double val = source_series->getCurrent(current_bar - i);
                if (!std::isnan(val))
                {
                    sum += val;
                    count++;
                }
            }

            if (count == 0)
            {
                result_series->setCurrent(current_bar, NAN);
            }
            else
            {
                double mean = sum / count;
                double sum_sq_diff = 0.0;
                for (int i = 0; i < length; ++i)
                {
                    double val = source_series->getCurrent(current_bar - i);
                    if (!std::isnan(val))
                    {
                        sum_sq_diff += (val - mean) * (val - mean);
                    }
                }
                // 使用样本标准差 (n-1)
                double std_dev = (count > 1) ? std::sqrt(sum_sq_diff / (count - 1)) : 0.0;
                result_series->setCurrent(current_bar, std_dev);
            }
        }
        return result_series;
    };
    built_in_funcs["stdp"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        auto *p = std::get_if<std::shared_ptr<Series>>(&source_val);
        if (!p)
        {
            return result_series;
        }
        auto source_series = *p;
        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));

        if (current_bar < length - 1)
        { // 数据不足
            result_series->setCurrent(current_bar, NAN);
        }
        else
        {
            double sum = 0.0;
            int count = 0;
            for (int i = 0; i < length; ++i)
            {
                double val = source_series->getCurrent(current_bar - i);
                if (!std::isnan(val))
                {
                    sum += val;
                    count++;
                }
            }

            if (count == 0)
            {
                result_series->setCurrent(current_bar, NAN);
            }
            else
            {
                double mean = sum / count;
                double sum_sq_diff = 0.0;
                for (int i = 0; i < length; ++i)
                {
                    double val = source_series->getCurrent(current_bar - i);
                    if (!std::isnan(val))
                    {
                        sum_sq_diff += (val - mean) * (val - mean);
                    }
                }
                // 使用总体标准差 (n)
                double std_dev = (count > 0) ? std::sqrt(sum_sq_diff / count) : 0.0;
                result_series->setCurrent(current_bar, std_dev);
            }
        }
        return result_series;
    };
    built_in_funcs["var"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        auto *p = std::get_if<std::shared_ptr<Series>>(&source_val);
        if (!p)
        {
            return result_series;
        }
        auto source_series = *p;
        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));

        if (current_bar < length - 1)
        { // 数据不足
            result_series->setCurrent(current_bar, NAN);
        }
        else
        {
            double sum = 0.0;
            int count = 0;
            for (int i = 0; i < length; ++i)
            {
                double val = source_series->getCurrent(current_bar - i);
                if (!std::isnan(val))
                {
                    sum += val;
                    count++;
                }
            }

            if (count == 0)
            {
                result_series->setCurrent(current_bar, NAN);
            }
            else
            {
                double mean = sum / count;
                double sum_sq_diff = 0.0;
                for (int i = 0; i < length; ++i)
                {
                    double val = source_series->getCurrent(current_bar - i);
                    if (!std::isnan(val))
                    {
                        sum_sq_diff += (val - mean) * (val - mean);
                    }
                }
                // 使用样本方差 (n-1)
                double variance = (count > 1) ? sum_sq_diff / (count - 1) : 0.0;
                result_series->setCurrent(current_bar, variance);
            }
        }
        return result_series;
    };
    built_in_funcs["varp"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        auto *p = std::get_if<std::shared_ptr<Series>>(&source_val);
        if (!p)
        {
            return result_series;
        }
        auto source_series = *p;
        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));

        if (current_bar < length - 1)
        { // 数据不足
            result_series->setCurrent(current_bar, NAN);
        }
        else
        {
            double sum = 0.0;
            int count = 0;
            for (int i = 0; i < length; ++i)
            {
                double val = source_series->getCurrent(current_bar - i);
                if (!std::isnan(val))
                {
                    sum += val;
                    count++;
                }
            }

            if (count == 0)
            {
                result_series->setCurrent(current_bar, NAN);
            }
            else
            {
                double mean = sum / count;
                double sum_sq_diff = 0.0;
                for (int i = 0; i < length; ++i)
                {
                    double val = source_series->getCurrent(current_bar - i);
                    if (!std::isnan(val))
                    {
                        sum_sq_diff += (val - mean) * (val - mean);
                    }
                }
                // 使用总体方差 (n)
                double variance = (count > 0) ? sum_sq_diff / count : 0.0;
                result_series->setCurrent(current_bar, variance);
            }
        }
        return result_series;
    };
    //逻辑函数
    built_in_funcs["cross"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value val2 = vm.pop();
        Value val1 = vm.pop();

        double dval1 = vm.getNumericValue(val1);
        double dval2 = vm.getNumericValue(val2);

        // 获取前一个K线柱的两个序列的值
        auto *p1 = std::get_if<std::shared_ptr<Series>>(&val1);
        double prev_dval1 = p1&&*p1 ? (*p1)->getCurrent(vm.getCurrentBarIndex() - 1) : dval1;
        auto *p2 = std::get_if<std::shared_ptr<Series>>(&val2);
        double prev_dval2 = p2&&*p2 ? (*p2)->getCurrent(vm.getCurrentBarIndex() - 1) : dval2;

        // 检查是否有NaN值
        if (std::isnan(dval1) || std::isnan(dval2) || std::isnan(prev_dval1) || std::isnan(prev_dval2))
        {
            result_series->setCurrent(current_bar, false); // 任何一个值为NaN，则交叉不成立
            return result_series;
        }

        // 判断是否发生交叉
        // (A > B 且 A[1] <= B[1]) 或 (A < B 且 A[1] >= B[1])
        bool cross_up = (dval1 > dval2) && (prev_dval1 <= prev_dval2);
        // bool cross_down = (dval1 < dval2) && (prev_dval1 >= prev_dval2);

        // return cross_up || cross_down;
        result_series->setCurrent(current_bar, cross_up);
        return result_series;
    };
    built_in_funcs["downnday"] = [](PineVM &vm) -> Value
    {
        //todo
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        return result_series;
    };
    built_in_funcs["every"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value condition_val = vm.pop();

        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto condition_series = std::get<std::shared_ptr<Series>>(condition_val);

        bool result = false;
        if (current_bar >= length - 1)
        { // 确保有足够的历史数据来检查
            bool all_true = true;
            for (int i = 0; i < length; ++i)
            {
                double val = condition_series->getCurrent(current_bar - i);
                if (std::isnan(val) || val == 0.0)
                { // 只要有一个为假或NaN，则不满足
                    all_true = false;
                    break;
                }
            }
            result = all_true;
        }
        result_series->setCurrent(current_bar, static_cast<double>(result));
        return result_series;
    };
    built_in_funcs["exist"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        // EXIST 存在
        // 是否存在.
        // 例如:
        // EXIST(CLOSE>OPEN,10) 
        // 表示10日内存在着阳线,第2个参数为常量(N应大于0)
        Value length_val = vm.pop();
        Value condition_val = vm.pop();

        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto condition_series = std::get<std::shared_ptr<Series>>(condition_val);

        bool result = false;
        for (int i = 0; i < length && current_bar - i >= 0; ++i)
        {
            double val = condition_series->getCurrent(current_bar - i);
            if (!std::isnan(val) && val != 0.0)
            {
                result = true;
                break;
            }
        }
        result_series->setCurrent(current_bar, static_cast<double>(result));
        
        return result_series;
    };
    built_in_funcs["last"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value val2 = vm.pop();
        Value val1 = vm.pop();
        Value condition_val = vm.pop();
        
        auto condition_series = std::get<std::shared_ptr<Series>>(condition_val);
        //当前bar往前val1到val2的区间内，condition是否一直成立, val1为0表示第一个bar，val2为0表示最近一个bar
        int start_offset = static_cast<int>(vm.getNumericValue(val1));
        int end_offset = static_cast<int>(vm.getNumericValue(val2));
        if(start_offset == 0)
            start_offset = current_bar;
        if(end_offset == 0)
            end_offset = 1;

        bool all_true_in_range = true;
        // 遍历从 current_bar - end_offset 到 current_bar - start_offset 的K线
        for (int i = end_offset; i >= start_offset; --i)
        {
            int bar_to_check = current_bar - i;
            if (bar_to_check < 0)
            {
                all_true_in_range = false; // 超出数据范围
                break;
            }
            double cond_val = condition_series->getCurrent(bar_to_check);
            if (std::isnan(cond_val) || cond_val == 0.0)
            {
                all_true_in_range = false;
                break;
            }
        }
        result_series->setCurrent(current_bar, static_cast<double>(all_true_in_range));
        return result_series;
    };
    built_in_funcs["longcross"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value val2 = vm.pop();
        Value val1 = vm.pop();

        double dval1 = vm.getNumericValue(val1);
        double dval2 = vm.getNumericValue(val2);

        // 获取前一个K线柱的两个序列的值
        auto *p1 = std::get_if<std::shared_ptr<Series>>(&val1);
        double prev_dval1 = p1 && *p1 ? (*p1)->getCurrent(vm.getCurrentBarIndex() - 1) : dval1;
        auto *p2 = std::get_if<std::shared_ptr<Series>>(&val2);
        double prev_dval2 = p2 && *p2 ? (*p2)->getCurrent(vm.getCurrentBarIndex() - 1) : dval2;

        // 检查是否有NaN值
        if (std::isnan(dval1) || std::isnan(dval2) || std::isnan(prev_dval1) || std::isnan(prev_dval2))
        {
            result_series->setCurrent(current_bar, false); // 任何一个值为NaN，则交叉不成立
            return result_series;
        }

        // 判断是否发生向上金叉 (A从下方穿过B)
        bool long_cross = (dval1 > dval2) && (prev_dval1 <= prev_dval2);

        result_series->setCurrent(current_bar, long_cross);
        return result_series;
    };
    built_in_funcs["nday"] = [](PineVM &vm) -> Value
    {
        //todo
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        return result_series;
    };
    built_in_funcs["not"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value val = vm.pop();
        double dval = vm.getNumericValue(val);
        if (std::isnan(dval))
        {
            result_series->setCurrent(current_bar, NAN);
        }
        else
        {
            result_series->setCurrent(current_bar, static_cast<double>(dval == 0.0));
        }
        return result_series;
    };
    built_in_funcs["upnday"] = [](PineVM &vm) -> Value
    {
        //todo
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        return result_series;
    };

    built_in_funcs["lv"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        auto *p = std::get_if<std::shared_ptr<Series>>(&source_val);
        if (!p)
        {
            return result_series;
        }
        auto source_series = *p;
        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));

        if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar]))
        {
            double lowest_val = NAN;
            bool first = true;
            for (int i = 0; i < length && current_bar - i >= 0; ++i)
            {
                double val = source_series->getCurrent(current_bar - i);
                if (!std::isnan(val))
                {
                    if (first)
                    {
                        lowest_val = val;
                        first = false;
                    }
                    else
                    {
                        lowest_val = std::min(lowest_val, val);
                    }
                }
            }
            result_series->setCurrent(current_bar, lowest_val);
        }
        return result_series;
    };
    built_in_funcs["hv"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        auto *p = std::get_if<std::shared_ptr<Series>>(&source_val);
        if (!p)
        {
            return result_series;
        }
        auto source_series = *p;
        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));

        if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar]))
        {
            double highest_val = NAN;
            bool first = true;
            for (int i = 0; i < length && current_bar - i >= 0; ++i)
            {
                double val = source_series->getCurrent(current_bar - i);
                if (!std::isnan(val))
                {
                    if (first)
                    {
                        highest_val = val;
                        first = false;
                    }
                    else
                    {
                        highest_val = std::max(highest_val, val);
                    }
                }
            }
            result_series->setCurrent(current_bar, highest_val);
        }
        return result_series;
    };
    built_in_funcs["isnull"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        int current_bar = vm.getCurrentBarIndex();
        Value val = vm.pop();
        double dval = vm.getNumericValue(val);
        if (std::isnan(dval))
        {
            result_series->setCurrent(current_bar, 1.0); // Hithink/TDX中，NaN被认为是空值，返回1.0
        }
        else
        {
            result_series->setCurrent(current_bar, 0.0); // 非NaN返回0.0
        }
        return result_series;
    };

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