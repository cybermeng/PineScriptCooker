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

    vars.clear();
    vars.reserve(bytecode.varNum);
    for (int i = 0; i < bytecode.varNum; ++i)
    {
        auto temp_series = std::make_shared<Series>();
        temp_series->name = "_tmp" + std::to_string(i);
        vars.push_back(temp_series);
    }
    
    // 清空绘图结果和函数状态缓存
    plotted_series.clear();
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
            if (ip->op == OpCode::ADD)
                pushNumbericValue(left + right, ip->operand);
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
                bool result = (left != 0.0 && !std::isnan(left)) && (right != 0.0 && !std::isnan(right));
                pushNumbericValue(result ? 1.0 : 0.0, ip->operand);
            }
            else if (ip->op == OpCode::LOGICAL_OR)
            {
                // 在Hithink中, 非0且非NaN为true, 结果为1.0或0.0
                bool result = (left != 0.0 && !std::isnan(left)) || (right != 0.0 && !std::isnan(right));
                pushNumbericValue(result ? 1.0 : 0.0, ip->operand);
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
        case OpCode::RENAME_SERIES:
        {
            Value name_val = pop();
            Value &series_val = stack.back(); // Peek at the top of the stack
            auto series_ptr = std::get<std::shared_ptr<Series>>(series_val);
            series_ptr->name = std::get<std::string>(name_val);
            break;
        }
        case OpCode::STORE_AND_PLOT_GLOBAL:
        {
            // 窥视（Peek），而不是弹出（Pop）。该值可能被后续指令（例如 POP）使用。
            Value &val_to_store = stack.back();
            Value &val_stored = storeGlobal(ip->operand, val_to_store); // 存储一个副本

            // Now handle plotting
            auto series_ptr = std::get<std::shared_ptr<Series>>(val_stored);

            // Check if this series is already registered for plotting
            auto it = std::find_if(plotted_series.begin(), plotted_series.end(),
                                   [&](const PlottedSeries &ps)
                                   {
                                       return ps.series.get() == series_ptr.get();
                                   });

            if (it == plotted_series.end())
            {
                plotted_series.push_back({series_ptr, "default_color"});
            }
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
                std::string cache_key = "__call__" + func_name + "~" + std::to_string(ip->operand);
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
        case OpCode::CALL_PLOT:
        {
            Value color_val = pop();          // 颜色
            Value series_to_plot_val = pop(); // 要绘制的序列

            auto series_ptr = std::get<std::shared_ptr<Series>>(series_to_plot_val);

            // 检查此序列是否已注册以进行绘制
            auto it = std::find_if(plotted_series.begin(), plotted_series.end(),
                                   [&](const PlottedSeries &ps)
                                   {
                                       return ps.series.get() == series_ptr.get();
                                   });

            if (it == plotted_series.end())
            {
                // 尚未注册，添加它
                if (series_ptr->name.empty())
                {
                    // 如果序列没有名称，尝试从常量池中获取一个
                    // 这通常发生在像 `plot(close)` 这样的情况下，`close` 是一个内置变量，
                    // 它的 Series 对象可能没有在编译时设置名称。
                    // 我们可以使用其在 `built_in_vars` 中的键作为名称。
                    for (const auto &pair : built_in_vars)
                    {
                        if (std::holds_alternative<std::shared_ptr<Series>>(pair.second) &&
                            std::get<std::shared_ptr<Series>>(pair.second).get() == series_ptr.get())
                        {
                            series_ptr->name = pair.first;
                            break;
                        }
                    }
                    if (series_ptr->name.empty())
                    {
                        series_ptr->name = "unnamed_series"; // 最后的备用名称
                    }
                }

                std::string color_str = "default_color";
                if (std::holds_alternative<std::string>(color_val))
                {
                    color_str = std::get<std::string>(color_val);
                }
                plotted_series.push_back({series_ptr, color_str});
            }
            push(true); // plot() 在PineScript中返回void，但我们的VM可能需要一个返回值
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
    built_in_funcs["ta.sma"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto source_series = std::get<std::shared_ptr<Series>>(source_val);

        // 仅当尚未为当前K线计算时才计算
        if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar]))
        {
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

            double sma_val = (count > 0) ? sum / count : NAN;
            result_series->setCurrent(current_bar, sma_val);
        }

        return result_series;
    };
    built_in_funcs["dma"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto source_series = std::get<std::shared_ptr<Series>>(source_val);

        if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar]))
        {
            double current_source_val = source_series->getCurrent(current_bar);
            double prev_dma = result_series->getCurrent(current_bar - 1);

            double dma_val;
            if (std::isnan(current_source_val))
            {
                dma_val = NAN;
            }
            else if (std::isnan(prev_dma))
            {
                // DMA的第一个值通常是NaN，或者用SMA播种
                // 这里我们选择在数据不足时返回NaN，直到有足够数据计算
                dma_val = NAN;
            }
            else
            {
                // DMA计算公式: DMA = (当前值 - 前一日DMA) / 周期 + 前一日DMA
                dma_val = (current_source_val - prev_dma) / length + prev_dma;
            }
            result_series->setCurrent(current_bar, dma_val);
        }
        return result_series;
    };
    built_in_funcs["ema"] = built_in_funcs["ta.ema"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto source_series = std::get<std::shared_ptr<Series>>(source_val);

        if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar]))
        {
            double current_source_val = source_series->getCurrent(current_bar);
            double prev_ema = result_series->getCurrent(current_bar - 1);

            double ema_val;
            if (std::isnan(current_source_val))
            {
                ema_val = NAN;
            }
            else if (std::isnan(prev_ema))
            {
                // 用SMA为EMA播种
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
                ema_val = (count == length) ? (sum / length) : NAN;
            }
            else
            {
                double alpha = 2.0 / (length + 1.0);
                ema_val = alpha * current_source_val + (1.0 - alpha) * prev_ema;
            }
            result_series->setCurrent(current_bar, ema_val);
        }

        return result_series;
    };

    built_in_funcs["ta.rsi"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto source_series = std::get<std::shared_ptr<Series>>(source_val);

        std::string src_name = source_series->name;
        std::string rsi_key = "ta.rsi(" + src_name + "~" + std::to_string(length) + ")";
        std::string gain_key = "__rsi_avg_gain(" + src_name + "~" + std::to_string(length) + ")";
        std::string loss_key = "__rsi_avg_loss(" + src_name + "~" + std::to_string(length) + ")";

        auto get_or_create = [&](const std::string &key, const std::string &name)
        {
            if (vm.builtin_func_cache.count(key))
                return vm.builtin_func_cache.at(key);
            auto series = std::make_shared<Series>();
            series->name = name;
            vm.builtin_func_cache[key] = series;
            return series;
        };

        auto rsi_series = get_or_create(rsi_key, rsi_key);
        auto gain_series = get_or_create(gain_key, gain_key);
        auto loss_series = get_or_create(loss_key, loss_key);

        if (current_bar >= rsi_series->data.size() || std::isnan(rsi_series->data[current_bar]))
        {
            if (current_bar == 0)
            {
                gain_series->setCurrent(current_bar, NAN);
                loss_series->setCurrent(current_bar, NAN);
                rsi_series->setCurrent(current_bar, NAN);
                return rsi_series;
            }

            double current_source = source_series->getCurrent(current_bar); // 修正：在播种阶段也需要获取当前值
            double prev_source = source_series->getCurrent(current_bar - 1);

            if (std::isnan(current_source) || std::isnan(prev_source))
            {
                gain_series->setCurrent(current_bar, NAN);
                loss_series->setCurrent(current_bar, NAN);
                rsi_series->setCurrent(current_bar, NAN);
                return rsi_series;
            }

            double change = current_source - prev_source;
            double current_gain = std::max(0.0, change);
            double current_loss = std::max(0.0, -change);

            double avg_gain, avg_loss;
            double prev_avg_gain = gain_series->getCurrent(current_bar - 1);

            if (std::isnan(prev_avg_gain))
            {
                // 用简单移动平均为AvgGain/AvgLoss播种
                double gain_sum = 0.0;
                double loss_sum = 0.0;
                int count = 0;
                for (int i = 0; i < length && current_bar - i > 0; ++i)
                {
                    double s1 = source_series->getCurrent(current_bar - i);
                    double s2 = source_series->getCurrent(current_bar - i - 1);
                    if (!std::isnan(s1) && !std::isnan(s2))
                    {
                        double chg = s1 - s2;
                        gain_sum += std::max(0.0, chg);
                        loss_sum += std::max(0.0, -chg);
                        count++;
                    }
                }
                if (count == length)
                {
                    avg_gain = gain_sum / length;
                    avg_loss = loss_sum / length;
                }
                else
                {
                    avg_gain = current_gain;                             // 至少用当前值初始化
                    avg_loss = current_loss > 0 ? current_loss : 0.0001; // 避免除以零
                }
            }
            else
            {
                // Wilder's smoothing
                double prev_avg_loss = loss_series->getCurrent(current_bar - 1);
                avg_gain = (prev_avg_gain * (length - 1) + current_gain) / length;
                avg_loss = (prev_avg_loss * (length - 1) + current_loss) / length;
            }

            gain_series->setCurrent(current_bar, avg_gain);
            loss_series->setCurrent(current_bar, avg_loss);

            if (std::isnan(avg_gain) || std::isnan(avg_loss))
            {
                rsi_series->setCurrent(current_bar, NAN);
            }
            else if (avg_loss == 0.0)
            {
                rsi_series->setCurrent(current_bar, 100.0);
            }
            else
            {
                double rs = avg_gain / avg_loss;
                double rsi = 100.0 - (100.0 / (1.0 + rs));
                rsi_series->setCurrent(current_bar, rsi);
            }
        }
        return rsi_series;
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

            // 仅当尚未为当前K线计算时才计算
            if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar]))
            {
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
        }
        return result_series;
    };
    built_in_funcs["max"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value val2 = vm.pop();
        Value val1 = vm.pop();

        double dval1 = vm.getNumericValue(val1);
        double dval2 = vm.getNumericValue(val2);

        if (std::isnan(dval1) || std::isnan(dval2))
        {
            return NAN;
        }
        return std::max(dval1, dval2);
    };
    built_in_funcs["min"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value val2 = vm.pop();
        Value val1 = vm.pop();

        double dval1 = vm.getNumericValue(val1);
        double dval2 = vm.getNumericValue(val2);

        if (std::isnan(dval1) || std::isnan(dval2))
        {
            return NAN;
        }
        return std::min(dval1, dval2);
    };
    built_in_funcs["abs"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value val = vm.pop();
        double dval = vm.getNumericValue(val);
        if (std::isnan(dval))
        {
            return NAN;
        }
        return std::abs(dval);
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

        if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar]))
        {
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
        }
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

        if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar]))
        {
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
        }
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

        // 仅当尚未为当前K线计算时才计算
        if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar]))
        {
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
        }

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

        if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar]))
        {
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
        }
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

        if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar]))
        {
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
        }
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

        if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar]))
        {
            bool result = false;
            if (current_bar >= length - 1)
            { // 确保有足够的历史数据来检查
                for (int i = 0; i < length; ++i)
                {
                    double val = condition_series->getCurrent(current_bar - i);
                    if (!std::isnan(val) && val != 0.0)
                    { // 只要有一个为真，则满足
                        result = true;
                        break;
                    }
                }
            }
            result_series->setCurrent(current_bar, static_cast<double>(result));
        }
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

        if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar]))
        {
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
        }
        return result_series;
    };
    built_in_funcs["barslast"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value condition_val = vm.pop();

        int current_bar = vm.getCurrentBarIndex();
        auto condition_series = std::get<std::shared_ptr<Series>>(condition_val);

        if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar]))
        {
            int bars_ago = -1; // -1 表示从未发生或当前K线发生
            for (int i = 1; i <= current_bar; ++i)
            { // 从前一个K线开始往前找
                double val = condition_series->getCurrent(current_bar - i);
                if (!std::isnan(val) && val != 0.0)
                { // 找到第一个非零（true）的K线
                    bars_ago = i;
                    break;
                }
            }
            if (bars_ago == -1)
            { // 如果循环结束仍未找到，检查当前K线
                double current_val = condition_series->getCurrent(current_bar);
                if (!std::isnan(current_val) && current_val != 0.0)
                {
                    bars_ago = 0; // 当前K线满足条件
                }
            }
            result_series->setCurrent(current_bar, static_cast<double>(bars_ago - 1));
        }
        return result_series;
    };
    built_in_funcs["barslastcount"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value condition_val = vm.pop();
        
        int current_bar = vm.getCurrentBarIndex();
        auto condition_series = std::get<std::shared_ptr<Series>>(condition_val);

        if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar]))
        {
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
        }
        return result_series;
    };
    built_in_funcs["barssince"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value condition_val = vm.pop();

        int current_bar = vm.getCurrentBarIndex();
        auto condition_series = std::get<std::shared_ptr<Series>>(condition_val);

        if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar]))
        {
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
        }
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

        if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar]))
        {
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
        }
        return result_series;
    };
    built_in_funcs["valuewhen"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value offset_val = vm.pop();
        Value condition_val = vm.pop();
        Value source_val = vm.pop();

        auto *src_p = std::get_if<std::shared_ptr<Series>>(&source_val);
        auto *cond_p = std::get_if<std::shared_ptr<Series>>(&condition_val);

        if (!src_p || !cond_p)
        {
            return result_series; // 错误处理或返回NaN
        }

        auto source_series = *src_p;
        auto condition_series = *cond_p;
        int current_bar = vm.getCurrentBarIndex();
        int offset = static_cast<int>(vm.getNumericValue(offset_val));

        if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar]))
        {
            double result_val = NAN;
            // 从当前K线开始往前找，直到找到满足条件的K线
            for (int i = 0; i <= current_bar; ++i)
            {
                double cond_val = condition_series->getCurrent(current_bar - i);
                if (!std::isnan(cond_val) && cond_val != 0.0)
                { // 找到满足条件的K线
                    // 在这个满足条件的K线上，获取source序列的偏移值
                    if ((current_bar - i - offset) >= 0)
                    {
                        result_val = source_series->getCurrent(current_bar - i - offset);
                    }
                    break; // 找到第一个满足条件的就停止
                }
            }
            result_series->setCurrent(current_bar, result_val);
        }
        return result_series;
    };
    built_in_funcs["exist"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value condition_val = vm.pop();

        int current_bar = vm.getCurrentBarIndex();
        auto condition_series = std::get<std::shared_ptr<Series>>(condition_val);

        if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar]))
        {
            bool exists = false;
            for (int i = 0; i <= current_bar; ++i)
            {
                double val = condition_series->getCurrent(i);
                if (!std::isnan(val) && val != 0.0)
                {
                    exists = true;
                    break;
                }
            }
            result_series->setCurrent(current_bar, static_cast<double>(exists));
        }
        return result_series;
    };
    built_in_funcs["last"] = [](PineVM &vm) -> Value
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

        if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar]))
        {
            double last_val = source_series->getCurrent(current_bar - offset);
            result_series->setCurrent(current_bar, last_val);
        }
        return result_series;
    };
    built_in_funcs["between"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value high_val = vm.pop();
        Value low_val = vm.pop();
        Value source_val = vm.pop();

        double source = vm.getNumericValue(source_val);
        double low = vm.getNumericValue(low_val);
        double high = vm.getNumericValue(high_val);

        if (std::isnan(source) || std::isnan(low) || std::isnan(high))
        {
            return NAN;
        }
        return static_cast<double>(source >= low && source <= high);
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
        if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar]))
        {
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
        }
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

        if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar]))
        {
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
        }
        return result_series;
    };
    built_in_funcs["round"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value val = vm.pop();
        double dval = vm.getNumericValue(val);
        if (std::isnan(dval))
        {
            return NAN;
        }
        return static_cast<double>(std::round(dval));
    };
    built_in_funcs["intpart"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value val = vm.pop();
        double dval = vm.getNumericValue(val);
        if (std::isnan(dval))
        {
            return NAN;
        }
        return static_cast<double>(static_cast<long long>(dval));
    };
    built_in_funcs["mod"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value divisor_val = vm.pop();
        Value dividend_val = vm.pop();

        double dividend = vm.getNumericValue(dividend_val);
        double divisor = vm.getNumericValue(divisor_val);

        if (std::isnan(dividend) || std::isnan(divisor) || divisor == 0.0)
        {
            return NAN;
        }
        return static_cast<double>(static_cast<long long>(dividend) % static_cast<long long>(divisor));
    };
    built_in_funcs["floor"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value val = vm.pop();
        double dval = vm.getNumericValue(val);
        if (std::isnan(dval))
        {
            return NAN;
        }
        return static_cast<double>(std::floor(dval));   
    };
    built_in_funcs["ln"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value val = vm.pop();
        double dval = vm.getNumericValue(val);
        if (std::isnan(dval))
        {
            return NAN;
        }
        return static_cast<double>(std::log(dval));
    };
    built_in_funcs["sqrt"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value val = vm.pop();
        double dval = vm.getNumericValue(val);
        if (std::isnan(dval))
        {
            return NAN;
        }
        return static_cast<double>(std::sqrt(dval));
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

        if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar]))
        {
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
        }
        return result_series;
    };
    built_in_funcs["not"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value val = vm.pop();
        double dval = vm.getNumericValue(val);
        if (std::isnan(dval))
        {
            return NAN;
        }
        return static_cast<double>(dval == 0.0); // Hithink/TDX中，非0为真，0为假。NOT操作符将非0转为0，0转为1。
    };
    built_in_funcs["isnull"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value val = vm.pop();
        double dval = vm.getNumericValue(val);
        if (std::isnan(dval))
        {
            return 1.0; // Hithink/TDX中，NaN被认为是空值，返回1.0
        }
        return 0.0; // 非NaN返回0.0
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

        if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar]))
        {
            double ref_val = source_series->getCurrent(current_bar - offset);
            result_series->setCurrent(current_bar, ref_val);
        }
        return result_series;
    };
    built_in_funcs["if"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        Value false_val = vm.pop();
        Value true_val = vm.pop();
        Value condition_val = vm.pop();

        bool condition = vm.getBoolValue(condition_val);

        if (condition)
        {
            return true_val;
        }
        else
        {
            return false_val;
        }
    };

    built_in_funcs["cross"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
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
            return false; // 任何一个值为NaN，则交叉不成立
        }

        // 判断是否发生交叉
        // (A > B 且 A[1] <= B[1]) 或 (A < B 且 A[1] >= B[1])
        bool cross_up = (dval1 > dval2) && (prev_dval1 <= prev_dval2);
        // bool cross_down = (dval1 < dval2) && (prev_dval1 >= prev_dval2);

        // return cross_up || cross_down;
        return cross_up;
    };

    built_in_funcs["input.int"] = [](PineVM &vm) -> Value
    {
        std::shared_ptr<Series> result_series = std::get<std::shared_ptr<Series>>(vm.pop());
        vm.pop();
        Value defval = vm.pop();
        return defval;
    };
}

void PineVM::printPlottedResults() const
{
    if (plotted_series.empty())
    {
        std::cout << "\n--- No Plotted Results ---" << std::endl;
        return;
    }

    // 查找名为 "time" 的序列
    std::shared_ptr<Series> time_series = nullptr;
    for (const auto &pair : built_in_vars)
    {
        if (pair.first == "time" && std::holds_alternative<std::shared_ptr<Series>>(pair.second))
        {
            time_series = std::get<std::shared_ptr<Series>>(pair.second);
            break;
        }
    }

    if (time_series)
    {
        std::cout << "\n--- Time Series (前10个和后10个值) ---" << std::endl;
        const auto &data = time_series->data;
        const size_t n = data.size();

        auto print_time_value = [](double val)
        {
            // 假设时间戳是YYYYMMDD格式的整数，转换为可读的日期
            if (std::isnan(val))
            {
                std::cout << "nan";
            }
            else
            {
                // long long date_int = static_cast<long long>(val);
                // std::cout << date_int;
                //  将Unix时间戳转换为YYYYMMDD格式
                time_t rawtime = static_cast<time_t>(val);
                struct tm *dt;
                char buffer[80];
                dt = localtime(&rawtime);
                strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", dt);
                std::cout << buffer;
            }
        };

        std::cout << "  Data (total " << n << " points): [";

        if (n <= 20)
        {
            for (size_t i = 0; i < n; ++i)
            {
                if (i > 0)
                    std::cout << ", ";
                print_time_value(data[i]);
            }
        }
        else
        {
            for (size_t i = 0; i < 10; ++i)
            {
                if (i > 0)
                    std::cout << ", ";
                print_time_value(data[i]);
            }
            std::cout << ", ...";
            for (size_t i = n - 10; i < n; ++i)
            {
                std::cout << ", ";
                print_time_value(data[i]);
            }
        }
        std::cout << "]" << std::endl;
    }
    std::cout << "\n--- Plotted Results (前10个和后10个值) ---" << std::endl;
    for (const auto &plotted : plotted_series)
    {
        std::cout << "Series: " << plotted.series->name << ", Color: " << plotted.color << std::endl;

        const auto &data = plotted.series->data;
        const size_t n = data.size();

        auto print_value = [](double val)
        {
            if (std::isnan(val))
            {
                std::cout << "nan";
            }
            else
            {
                std::cout << std::fixed << std::setprecision(3) << val;
            }
        };

        std::cout << "  Data (total " << n << " points): [";

        if (n <= 20)
        {
            // 如果点数少于等于20，则全部打印
            for (size_t i = 0; i < n; ++i)
            {
                if (i > 0)
                    std::cout << ", ";
                print_value(data[i]);
            }
        }
        else
        {
            // 打印前10个点
            for (size_t i = 0; i < 10; ++i)
            {
                if (i > 0)
                    std::cout << ", ";
                print_value(data[i]);
            }
            std::cout << ", ...";
            // 打印后10个点
            for (size_t i = n - 10; i < n; ++i)
            {
                std::cout << ", ";
                print_value(data[i]);
            }
        }
        std::cout << "]" << std::endl;
    }
}

// 1. 私有辅助函数 (核心逻辑)
void PineVM::writePlottedResultsToStream(std::ostream &stream, int precision) const
{
    if (plotted_series.empty())
    {
        std::cout << "\n--- No Plotted Results ---" << std::endl;
        return;
    }
    // 查找名为 "time" 的序列
    std::shared_ptr<Series> time_series = nullptr;
    for (const auto &pair : built_in_vars)
    {
        if (pair.first == "time" && std::holds_alternative<std::shared_ptr<Series>>(pair.second))
        {
            time_series = std::get<std::shared_ptr<Series>>(pair.second);
            break;
        }
    }

    // 写入CSV头
    bool first_series = true;
    if (time_series)
    {
        stream << "time";
        first_series = false;
    }
    for (const auto &plotted : plotted_series)
    {
        if (!first_series)
        {
            stream << ",";
        }
        stream << plotted.series->name;
        first_series = false;
    }
    stream << "\n";

    // 写入数据
    if (!plotted_series.empty() || time_series)
    {
        size_t max_data_points = 0;
        if (time_series)
        {
            max_data_points = time_series->data.size();
        }
        for (const auto &plotted : plotted_series)
        {
            if (plotted.series->data.size() > max_data_points)
            {
                max_data_points = plotted.series->data.size();
            }
        }

        for (size_t i = 0; i < max_data_points; ++i)
        {
            first_series = true;
            if (time_series)
            {
                if (i < time_series->data.size())
                {
                    char buffer[80] = {};
                    // 将Unix时间戳转换为YYYYMMDD格式
                    time_t rawtime = static_cast<time_t>(time_series->data[i]);
                    if (rawtime > 0)
                    {
                        struct tm *dt;
                        dt = localtime(&rawtime);
                        if(dt)
                            strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", dt);
                    }
                    stream << std::fixed << buffer;
                }
                first_series = false;
            }
            for (const auto &plotted : plotted_series)
            {
                if (!first_series)
                {
                    stream << ",";
                }
                if (i < plotted.series->data.size())
                {
                    // 数据保留N位小数
                    stream << std::fixed << std::setprecision(precision) << plotted.series->data[i];
                }
                first_series = false;
            }
            stream << "\n";
        }
    }
}

// 2. 新的公共接口：写入文件
void PineVM::writePlottedResultsToFile(const std::string &filename, int precision) const
{
    std::ofstream outfile(filename);
    if (!outfile.is_open())
    {
        std::cerr << "Error: Could not open file " << filename << " for writing." << std::endl;
        return;
    }

    // 调用核心逻辑函数
    writePlottedResultsToStream(outfile, precision);

    outfile.close(); // ofstream 在析构时会自动关闭，但显式关闭也是好习惯
    std::cout << "Plotted results written to " << filename << std::endl;
}

// 3. 新的公共接口：输出为字符串
std::string PineVM::getPlottedResultsAsString() const
{
    std::stringstream ss;

    // 调用核心逻辑函数
    writePlottedResultsToStream(ss);

    // 从 stringstream 获取字符串并返回
    return ss.str();
}
