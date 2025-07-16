#include "PineVM.h"
#include <iostream>
#include <iomanip>
#include <numeric>
#include <algorithm> // For std::find_if
#include <fstream> // For file output
#include <sstream> // For std::stringstream
#include <map> // For opCodeMap in txtToBytecode
#include <optional> // For std::optional in txtToBytecode
#include <cstdint> // for uint64_t

double Series::getCurrent(int bar_index) {
    if (bar_index >= 0 && bar_index < data.size()) {
        return data[bar_index];
    }
    // 如果索引超出范围或数据未加载，返回 NaN
    return NAN;
}

// ... Series::setCurrent 保持不变 ...
void Series::setCurrent(int bar_index, double value) {
    if (bar_index >= data.size()) {
        data.resize(bar_index + 1, NAN);
    }
    data[bar_index] = value;
}

void Series::setName(const std::string& name) {
    this->name = name;
}

// PineVM 构造函数现在负责打开 DuckDB 数据库和连接
PineVM::PineVM(int total_bars)
    : total_bars(total_bars), bar_index(0), ip(nullptr) {
    registerBuiltins();
}

PineVM::~PineVM() {
}

void PineVM::loadBytecode(const std::string& code) {
    std::cout << "----- Loading bytecode -----" << std::endl;
    bytecode = PineVM::txtToBytecode(code); // This line is causing the error
    std::cout << PineVM::bytecodeToTxt(bytecode);
}

int PineVM::execute() {
    globals.resize(bytecode.global_name_pool.size());

    vars.clear(); // 清理旧数据
    vars.reserve(bytecode.varNum); // 预分配内存以提高效率
     for (int i = 0; i < bytecode.varNum; ++i) {
        auto temp_series = std::make_shared<Series>();
        // 为了调试方便，可以给中间变量序列一个名字
        temp_series->name = "_tmp" + std::to_string(i); 
        vars.push_back(temp_series);
    }

    plotted_series.clear();
    try
    {
        for (bar_index = 0; bar_index < total_bars; ++bar_index)
        {
            // std::cout << "--- Executing Bar #" << bar_index << " ---" << std::endl; // 注释掉以保持输出整洁
            runCurrentBar();
        }
    } catch (const std::exception& e) {
        std::cerr << "PineVM::execute Error: " << e.what()
         << " @bar_index: " << bar_index
         << " @ip: " << int(ip - &bytecode.instructions[0])
         << std::endl;
        return 1;
    }
    return 0;
}

void PineVM::push(Value val) {
    stack.push_back(std::move(val));
}

Value PineVM::pop() {
    if (stack.empty()) throw std::runtime_error("Stack underflow!");
    Value val = stack.back();
    stack.pop_back();
    return val;
}

double PineVM::getNumericValue(const Value& val) {
    if (auto* p = std::get_if<double>(&val)) {
        return *p;
    }
    if (auto* p = std::get_if<std::shared_ptr<Series>>(&val)) {
        return *p ? (*p)->getCurrent(bar_index) : NAN;
    }
    if (std::holds_alternative<bool>(val)){
        return static_cast<double>(std::get<bool>(val));            
    }
    if (std::holds_alternative<std::monostate>(val)){
        return NAN;            
    }
    throw std::runtime_error("Unsupported operand type for numeric operation.");
 }

bool PineVM::getBoolValue(const Value& val) {
    if (auto* p = std::get_if<bool>(&val)) {
        return *p;
    }
    if (auto* p = std::get_if<std::shared_ptr<Series>>(&val)) {
        return *p ? static_cast<bool>((*p)->getCurrent(bar_index)) : NAN;
    }
    if (std::holds_alternative<double>(val)){
        return static_cast<bool>(std::get<double>(val));            
    }
    if (std::holds_alternative<std::monostate>(val)){
        return NAN;            
    }
    throw std::runtime_error("Unsupported operand type for bool operation.");
}

void PineVM::pushNumbericValue(double val, int operand) {
    // 这个函数处理算术/逻辑运算的结果。
    // 在PineScript中，这些操作通常产生一个新的序列。
    // 'operand' 参数是编译器分配的中间变量槽的索引，
    // 用于存储这个新序列。

    // 检查操作数是否是有效的中间变量索引
    if (operand < 0 || operand >= vars.size()) {
        // 这是一个严重的字节码错误。如果一个操作需要存储中间结果，
        // 编译器必须提供一个有效的槽位索引。
        // 一个简单的回退方案是直接压入double值，但这会破坏序列的连续性。
        // push(val); // 这是一个不正确的简化方案
        throw std::runtime_error("Invalid intermediate variable index (" + std::to_string(operand) + ") for arithmetic/logic operation. Max index is " + std::to_string(vars.size() - 1) + ".");
    }

    // 1. 从预先分配的池中获取中间序列
    auto& result_series = vars[operand];

    // 2. 为当前K线柱设置计算出的值
    result_series->setCurrent(bar_index, val);

    // 3. 将这个（现在已更新的）序列的智能指针压入栈中，
    // 以便后续操作（如另一个算术运算或存储到全局变量）可以使用它。
    push(result_series);
}

Value& PineVM::storeGlobal(int operand, const Value& val) {
        // 检查全局变量槽位是否已经是一个Series
    if (std::holds_alternative<std::shared_ptr<Series>>(globals[operand])) {
        auto series_ptr = std::get<std::shared_ptr<Series>>(globals[operand]);
        if (std::holds_alternative<double>(val) || std::holds_alternative<bool>(val)) {
            // 如果弹出的值是double，则设置Series的当前bar值
            series_ptr->setCurrent(bar_index,
                std::holds_alternative<bool>(val) ? static_cast<double>(std::get<bool>(val)) : std::get<double>(val));
        } else if (std::holds_alternative<std::shared_ptr<Series>>(val)) {
            // 如果弹出的值是Series，则替换bar对应数值
            auto val_series_ptr = std::get<std::shared_ptr<Series>>(val);
            series_ptr->setCurrent(bar_index, val_series_ptr->getCurrent(bar_index));
        } else {
            // 尝试将其他类型的值存储到Series中（可能需要转换或抛出错误）
            throw std::runtime_error("Attempted to store unsupported type into existing Series global.");
        }
    } else if (std::holds_alternative<std::monostate>(globals[operand])) {
        // 如果是monostate，说明这个槽位是空的，可以根据弹出的值类型来初始化
        if (std::holds_alternative<double>(val) || std::holds_alternative<bool>(val)) {
            // 如果是double/bool，创建一个新的Series来存储它
            auto new_series = std::make_shared<Series>();
            new_series->setCurrent(bar_index,
                std::holds_alternative<bool>(val) ? static_cast<double>(std::get<bool>(val)) : std::get<double>(val));
            new_series->setName(bytecode.global_name_pool[operand]);
            globals[operand] = new_series;
        } else {
            // 其他类型直接存储
            globals[operand] = val;
            auto series_ptr = std::get<std::shared_ptr<Series>>(globals[operand]);
            series_ptr->setName(bytecode.global_name_pool[operand]);
        }

    } else {
        // 如果不是Series，直接存储弹出的值
        globals[operand] = val;
        auto series_ptr = std::get<std::shared_ptr<Series>>(globals[operand]);
        series_ptr->setName(bytecode.global_name_pool[operand]);
    }
    return globals[operand];
}

void PineVM::runCurrentBar() {
    ip = &bytecode.instructions[0];

    while (ip->op != OpCode::HALT) {
        switch (ip->op) {
            case OpCode::PUSH_CONST: {
                push(bytecode.constant_pool[ip->operand]);
                break;
            }
            case OpCode::POP: {
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
            case OpCode::LOGICAL_OR: {
                double right = getNumericValue(pop());
                double left = getNumericValue(pop());
                if (ip->op == OpCode::ADD) pushNumbericValue(left + right, ip->operand);
                else if (ip->op == OpCode::DIV) {
                    if (right == 0.0) {
                        pushNumbericValue(NAN, ip->operand);
                    } else pushNumbericValue(left / right, ip->operand);
                }
                else if (ip->op == OpCode::SUB) pushNumbericValue(left - right, ip->operand);
                else if (ip->op == OpCode::MUL) pushNumbericValue(left * right, ip->operand);
                else if (ip->op == OpCode::DIV) pushNumbericValue(left / right, ip->operand);
                else if (ip->op == OpCode::LESS) pushNumbericValue(left < right, ip->operand);
                else if (ip->op == OpCode::LESS_EQUAL) pushNumbericValue(left <= right, ip->operand);
                else if (ip->op == OpCode::EQUAL_EQUAL) pushNumbericValue(left == right, ip->operand);
                else if (ip->op == OpCode::BANG_EQUAL) pushNumbericValue(left != right, ip->operand);
                else if (ip->op == OpCode::GREATER) pushNumbericValue(left > right, ip->operand);
                else if (ip->op == OpCode::GREATER_EQUAL) pushNumbericValue(left >= right, ip->operand);
                else if (ip->op == OpCode::LOGICAL_AND) {
                    // 在Hithink中, 非0且非NaN为true, 结果为1.0或0.0
                    bool result = (left != 0.0 && !std::isnan(left)) && (right != 0.0 && !std::isnan(right));
                    pushNumbericValue(result ? 1.0 : 0.0, ip->operand);
                }
                else if (ip->op == OpCode::LOGICAL_OR) {
                    // 在Hithink中, 非0且非NaN为true, 结果为1.0或0.0
                    bool result = (left != 0.0 && !std::isnan(left)) || (right != 0.0 && !std::isnan(right));
                    pushNumbericValue(result ? 1.0 : 0.0, ip->operand);
                }
                break;
                }
            case OpCode::LOAD_GLOBAL: {
                push(globals[ip->operand]);
                break;
            }
            case OpCode::STORE_GLOBAL: {
                storeGlobal(ip->operand, pop());
                break;
            }
            case OpCode::RENAME_SERIES: {
                Value name_val = pop();
                Value& series_val = stack.back(); // Peek at the top of the stack
                auto series_ptr = std::get<std::shared_ptr<Series>>(series_val);
                series_ptr->name = std::get<std::string>(name_val);
                break;
            }
            case OpCode::STORE_AND_PLOT_GLOBAL: {
                // 窥视（Peek），而不是弹出（Pop）。该值可能被后续指令（例如 POP）使用。
                Value& val_to_store = stack.back();
                Value& val_stored = storeGlobal(ip->operand, val_to_store); // 存储一个副本

                // Now handle plotting
                auto series_ptr = std::get<std::shared_ptr<Series>>(val_stored);

                // Check if this series is already registered for plotting
                auto it = std::find_if(plotted_series.begin(), plotted_series.end(),
                                       [&](const PlottedSeries& ps) {
                                           return ps.series.get() == series_ptr.get();
                                       });

                if (it == plotted_series.end()) {
                    plotted_series.push_back({series_ptr, "default_color"});
                }
                break;
            }
            case OpCode::LOAD_BUILTIN_VAR: {
                const std::string& name = std::get<std::string>(bytecode.constant_pool[ip->operand]);
                if (built_in_vars.count(name)) {
                    push(built_in_vars.at(name));
                } else {
                    throw std::runtime_error("Undefined built-in variable: " + name);
                }
                break;
            }
            case OpCode::JUMP_IF_FALSE: {
                Value condition = pop();
                if (!std::get<bool>(condition)) {
                    ip += ip->operand; // Jump forward
                    continue; // Skip the default ip++
                }
                break;
            }
            case OpCode::JUMP:
                ip += ip->operand; // Jump forward
                break;
            case OpCode::CALL_BUILTIN_FUNC: {
                const std::string& func_name = std::get<std::string>(bytecode.constant_pool[ip->operand]);
                 if (built_in_funcs.count(func_name)) {
                    Value result = built_in_funcs.at(func_name)(*this);
                    push(result);
                } else {
                    throw std::runtime_error("Undefined built-in function: " + func_name);
                }
                break;
            }
            case OpCode::CALL_PLOT: {
                Value color_val = pop(); // 颜色
                Value series_to_plot_val = pop(); // 要绘制的序列

                auto series_ptr = std::get<std::shared_ptr<Series>>(series_to_plot_val);

                // 检查此序列是否已注册以进行绘制
                auto it = std::find_if(plotted_series.begin(), plotted_series.end(),
                                       [&](const PlottedSeries& ps) {
                                           return ps.series.get() == series_ptr.get();
                                       });

                if (it == plotted_series.end()) {
                    // 尚未注册，添加它
                    if (series_ptr->name.empty()) {
                        // 如果序列没有名称，尝试从常量池中获取一个
                        // 这通常发生在像 `plot(close)` 这样的情况下，`close` 是一个内置变量，
                        // 它的 Series 对象可能没有在编译时设置名称。
                        // 我们可以使用其在 `built_in_vars` 中的键作为名称。
                        for (const auto& pair : built_in_vars) {
                            if (std::holds_alternative<std::shared_ptr<Series>>(pair.second) &&
                                std::get<std::shared_ptr<Series>>(pair.second).get() == series_ptr.get()) {
                                series_ptr->name = pair.first;
                                break;
                            }
                        }
                        if (series_ptr->name.empty()) {
                            series_ptr->name = "unnamed_series"; // 最后的备用名称
                        }
                    }

                    std::string color_str = "default_color";
                    if (std::holds_alternative<std::string>(color_val)) {
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

void PineVM::registerSeries(const std::string& name, std::shared_ptr<Series> series) {
    built_in_vars[name] = series;
}

void PineVM::registerBuiltins() {
    built_in_funcs["ta.sma"] = [](PineVM& vm) -> Value {
        Value length_val = vm.pop();
        Value source_val = vm.pop();
        
        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto source_series = std::get<std::shared_ptr<Series>>(source_val);

        // 基于函数和参数创建唯一的缓存键，以支持状态保持
        std::string cache_key = "ta.sma(" + source_series->name + "~" + std::to_string(length) + ")";

        std::shared_ptr<Series> result_series;
        if (vm.builtin_func_cache.count(cache_key)) {
            result_series = vm.builtin_func_cache.at(cache_key);
        } else {
            result_series = std::make_shared<Series>();
            result_series->name = cache_key;
            vm.builtin_func_cache[cache_key] = result_series;
        }
        
        // 仅当尚未为当前K线计算时才计算
        if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar])) {
            double sum = 0.0;
            int count = 0;
            for (int i = 0; i < length && current_bar - i >= 0; ++i) {
                double val = source_series->getCurrent(current_bar - i);
                if (!std::isnan(val)) {
                    sum += val;
                    count++;
                }
            }
            
            double sma_val = (count > 0) ? sum / count : NAN;
            result_series->setCurrent(current_bar, sma_val);
        }

        return result_series;
    };
    
    built_in_funcs["ema"] = built_in_funcs["ta.ema"] = [](PineVM& vm) -> Value {
        Value length_val = vm.pop();
        Value source_val = vm.pop();
        
        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto source_series = std::get<std::shared_ptr<Series>>(source_val);

        std::string cache_key = "ema(" + source_series->name + "~" + std::to_string(length) + ")";

        std::shared_ptr<Series> result_series;
        if (vm.builtin_func_cache.count(cache_key)) {
            result_series = vm.builtin_func_cache.at(cache_key);
        } else {
            result_series = std::make_shared<Series>();
            result_series->name = cache_key;
            vm.builtin_func_cache[cache_key] = result_series;
        }
        
        if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar])) {
            double current_source_val = source_series->getCurrent(current_bar);
            double prev_ema = result_series->getCurrent(current_bar - 1);

            double ema_val;
            if (std::isnan(current_source_val)) {
                ema_val = NAN;
            } else if (std::isnan(prev_ema)) {
                // 用SMA为EMA播种
                double sum = 0.0;
                int count = 0;
                for (int i = 0; i < length && current_bar - i >= 0; ++i) {
                    double val = source_series->getCurrent(current_bar - i);
                    if (!std::isnan(val)) {
                        sum += val;
                        count++;
                    }
                }
                ema_val = (count == length) ? (sum / length) : NAN;
            } else {
                double alpha = 2.0 / (length + 1.0);
                ema_val = alpha * current_source_val + (1.0 - alpha) * prev_ema;
            }
            result_series->setCurrent(current_bar, ema_val);
        }

        return result_series;
    };

    built_in_funcs["ta.rsi"] = [](PineVM& vm) -> Value {
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto source_series = std::get<std::shared_ptr<Series>>(source_val);

        std::string src_name = source_series->name;
        std::string rsi_key = "ta.rsi(" + src_name + "~" + std::to_string(length) + ")";
        std::string gain_key = "__rsi_avg_gain(" + src_name + "~" + std::to_string(length) + ")";
        std::string loss_key = "__rsi_avg_loss(" + src_name + "~" + std::to_string(length) + ")";

        auto get_or_create = [&](const std::string& key, const std::string& name) {
            if (vm.builtin_func_cache.count(key)) return vm.builtin_func_cache.at(key);
            auto series = std::make_shared<Series>();
            series->name = name;
            vm.builtin_func_cache[key] = series;
            return series;
        };

        auto rsi_series = get_or_create(rsi_key, rsi_key);
        auto gain_series = get_or_create(gain_key, gain_key);
        auto loss_series = get_or_create(loss_key, loss_key);

        if (current_bar >= rsi_series->data.size() || std::isnan(rsi_series->data[current_bar])) {
            if (current_bar == 0) {
                gain_series->setCurrent(current_bar, NAN);
                loss_series->setCurrent(current_bar, NAN);
                rsi_series->setCurrent(current_bar, NAN);
                return rsi_series;
            }

            double current_source = source_series->getCurrent(current_bar); // 修正：在播种阶段也需要获取当前值
            double prev_source = source_series->getCurrent(current_bar - 1);

            if (std::isnan(current_source) || std::isnan(prev_source)) {
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

             if (std::isnan(prev_avg_gain)) {
                 // 用简单移动平均为AvgGain/AvgLoss播种
                 double gain_sum = 0.0;
                 double loss_sum = 0.0;
                 int count = 0;
                 for (int i = 0; i < length && current_bar - i > 0; ++i) {
                     double s1 = source_series->getCurrent(current_bar - i);
                     double s2 = source_series->getCurrent(current_bar - i - 1);
                     if (!std::isnan(s1) && !std::isnan(s2)) {
                         double chg = s1 - s2;
                         gain_sum += std::max(0.0, chg);
                         loss_sum += std::max(0.0, -chg);
                         count++;
                     }
                 }
                 if (count == length) {
                     avg_gain = gain_sum / length;
                     avg_loss = loss_sum / length;
                 } else {
                     avg_gain = current_gain; // 至少用当前值初始化
                     avg_loss = current_loss > 0 ? current_loss : 0.0001; // 避免除以零
                 }
             }
             else {
                // Wilder's smoothing
                double prev_avg_loss = loss_series->getCurrent(current_bar - 1);
                avg_gain = (prev_avg_gain * (length - 1) + current_gain) / length;
                avg_loss = (prev_avg_loss * (length - 1) + current_loss) / length;
            }

            gain_series->setCurrent(current_bar, avg_gain);
            loss_series->setCurrent(current_bar, avg_loss);

            if (std::isnan(avg_gain) || std::isnan(avg_loss)) {
                rsi_series->setCurrent(current_bar, NAN);
            } else if (avg_loss == 0.0) {
                rsi_series->setCurrent(current_bar, 100.0);
            } else {
                double rs = avg_gain / avg_loss;
                double rsi = 100.0 - (100.0 / (1.0 + rs));
                rsi_series->setCurrent(current_bar, rsi);
            }
        }
        return rsi_series;
    };
    
    built_in_funcs["ma"] = [](PineVM& vm) -> Value {
        Value length_val = vm.pop();
        Value source_val = vm.pop();
        
        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));

        std::shared_ptr<Series> result_series;

        auto *p = std::get_if<std::shared_ptr<Series>>(&source_val);
        if(p) {
            auto source_series = *p;

            // 基于函数和参数创建唯一的缓存键，以支持状态保持
            std::string cache_key = "MA(" + source_series->name + "~" + std::to_string(length) + ")";

            if (vm.builtin_func_cache.count(cache_key)) {
                result_series = vm.builtin_func_cache.at(cache_key);
            } else {
                result_series = std::make_shared<Series>();
                result_series->name = cache_key;
                vm.builtin_func_cache[cache_key] = result_series;
            }
            
            // 仅当尚未为当前K线计算时才计算
            if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar])) {
                double sum = 0.0;
                int count = 0;
                for (int i = 0; i < length && current_bar - i >= 0; ++i) {
                    double val = source_series->getCurrent(current_bar - i);
                    if (!std::isnan(val)) {
                        sum += val;
                        count++;
                    }
                }
                
                double ma_val = (count == length)? sum / count : NAN;
                result_series->setCurrent(current_bar, ma_val);
            }
        }
        return result_series;
    };
    built_in_funcs["max"] = [](PineVM& vm) -> Value {
        Value val2 = vm.pop();
        Value val1 = vm.pop();

        double dval1 = vm.getNumericValue(val1);
        double dval2 = vm.getNumericValue(val2);

        if (std::isnan(dval1) || std::isnan(dval2)) {
            return NAN;
        }
        return std::max(dval1, dval2);
    };
    built_in_funcs["min"] = [](PineVM& vm) -> Value {
        Value val2 = vm.pop();
        Value val1 = vm.pop();

        double dval1 = vm.getNumericValue(val1);
        double dval2 = vm.getNumericValue(val2);

        if (std::isnan(dval1) || std::isnan(dval2)) {
            return NAN;
        }
        return std::min(dval1, dval2);
    };
    built_in_funcs["abs"] = [](PineVM& vm) -> Value {
        Value val = vm.pop();
        double dval = vm.getNumericValue(val);
        if (std::isnan(dval)) {
            return NAN;
        }
        return std::abs(dval);
    };
    built_in_funcs["llv"] = [](PineVM& vm) -> Value {
        Value length_val = vm.pop();
        Value source_val = vm.pop();

         std::shared_ptr<Series> result_series;

        auto* p = std::get_if<std::shared_ptr<Series>>(&source_val);
        if (!p) {
            return result_series;
        }
        auto source_series = *p;
        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));
 
        std::string cache_key = "LLV(" + source_series->name + "~" + std::to_string(length) + ")";

        if (vm.builtin_func_cache.count(cache_key)) {
            result_series = vm.builtin_func_cache.at(cache_key);
        } else {
            result_series = std::make_shared<Series>();
            result_series->name = cache_key;
            vm.builtin_func_cache[cache_key] = result_series;
        }

        if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar])) {
            double lowest_val = NAN;
            bool first = true;
            for (int i = 0; i < length && current_bar - i >= 0; ++i) {
                double val = source_series->getCurrent(current_bar - i);
                if (!std::isnan(val)) {
                    if (first) {
                        lowest_val = val;
                        first = false;
                    } else {
                        lowest_val = std::min(lowest_val, val);
                    }
                }
            }
            result_series->setCurrent(current_bar, lowest_val);
        }
        return result_series;
    };

    built_in_funcs["hhv"] = [](PineVM& vm) -> Value {
        Value length_val = vm.pop();
        Value source_val = vm.pop();

         std::shared_ptr<Series> result_series;

        auto* p = std::get_if<std::shared_ptr<Series>>(&source_val);
        if (!p) {
            return result_series;
        }
        auto source_series = *p;
        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));
 
        std::string cache_key = "HHV(" + source_series->name + "~" + std::to_string(length) + ")";

        if (vm.builtin_func_cache.count(cache_key)) {
            result_series = vm.builtin_func_cache.at(cache_key);
        } else {
            result_series = std::make_shared<Series>();
            result_series->name = cache_key;
            vm.builtin_func_cache[cache_key] = result_series;
        }

        if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar])) {
            double highest_val = NAN;
            bool first = true;
            for (int i = 0; i < length && current_bar - i >= 0; ++i) {
                double val = source_series->getCurrent(current_bar - i);
                if (!std::isnan(val)) {
                    if (first) {
                        highest_val = val;
                        first = false;
                    } else {
                        highest_val = std::max(highest_val, val);
                    }
                }
            }
            result_series->setCurrent(current_bar, highest_val);
        }
        return result_series;
    };
    built_in_funcs["sma"] = [](PineVM& vm) -> Value {
         // Hithink/TDX SMA函数有三个参数: SMA(X,N,M)
        // X: 源数据, N: 周期, M: 权重 (通常为1, 表示简单移动平均)
        Value weight_val = vm.pop(); // M
        Value length_val = vm.pop();
        Value source_val = vm.pop();
        
        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto source_series = std::get<std::shared_ptr<Series>>(source_val);

        // 基于函数和参数创建唯一的缓存键，以支持状态保持
        std::string cache_key = "SMA(" + source_series->name + "~" + std::to_string(length) + ")";

        std::shared_ptr<Series> result_series;
        if (vm.builtin_func_cache.count(cache_key)) {
            result_series = vm.builtin_func_cache.at(cache_key);
        } else {
            result_series = std::make_shared<Series>();
            result_series->name = cache_key;
            vm.builtin_func_cache[cache_key] = result_series;
        }
        
        // 仅当尚未为当前K线计算时才计算
        if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar])) {
            double sum = 0.0;
            int count = 0;
            for (int i = 0; i < length && current_bar - i >= 0; ++i) {
                double val = source_series->getCurrent(current_bar - i);
                if (!std::isnan(val)) {
                    sum += val;
                    count++;
                }
            }
            
            double sma_val = (count == length)? sum / count : NAN;
            result_series->setCurrent(current_bar, sma_val);
        }

        return result_series;
    };
    built_in_funcs["sum"] = [](PineVM& vm) -> Value {
        Value length_val = vm.pop();
        Value source_val = vm.pop();

        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto source_series = std::get<std::shared_ptr<Series>>(source_val);

        std::string cache_key = "SUM(" + source_series->name + "~" + std::to_string(length) + ")";

        std::shared_ptr<Series> result_series;
        if (vm.builtin_func_cache.count(cache_key)) {
            result_series = vm.builtin_func_cache.at(cache_key);
        } else {
            result_series = std::make_shared<Series>();
            result_series->name = cache_key;
            vm.builtin_func_cache[cache_key] = result_series;
        }

        if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar])) {
            double sum = 0.0;
            int count = 0;
            for (int i = 0; i < length && current_bar - i >= 0; ++i) {
                double val = source_series->getCurrent(current_bar - i);
                if (!std::isnan(val)) {
                    sum += val;
                    count++;
                }
            }
            double sum_val = (count == length) ? sum : NAN;
            result_series->setCurrent(current_bar, sum_val);
        }
        return result_series;
    };

    built_in_funcs["count"] = [](PineVM& vm) -> Value {
        Value length_val = vm.pop();
        Value condition_val = vm.pop();

        int current_bar = vm.getCurrentBarIndex();
        int length = static_cast<int>(vm.getNumericValue(length_val));
        auto condition_series = std::get<std::shared_ptr<Series>>(condition_val);

        std::string cache_key = "COUNT(" + condition_series->name + "~" + std::to_string(length) + ")";

        std::shared_ptr<Series> result_series;
        if (vm.builtin_func_cache.count(cache_key)) {
            result_series = vm.builtin_func_cache.at(cache_key);
        } else {
            result_series = std::make_shared<Series>();
            result_series->name = cache_key;
            vm.builtin_func_cache[cache_key] = result_series;
        }

        if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar])) {
            int count = 0;
            for (int i = 0; i < length && current_bar - i >= 0; ++i) {
                double val = condition_series->getCurrent(current_bar - i);
                if (!std::isnan(val) && val != 0.0) { // Assuming non-zero or non-NaN means true
                    count++;
                }
            }
            result_series->setCurrent(current_bar, static_cast<double>(count));
        }
        return result_series;
    };
    built_in_funcs["ref"] = [](PineVM& vm) -> Value {
        Value offset_val = vm.pop();
        Value source_val = vm.pop();
        std::shared_ptr<Series> result_series;

        auto* p = std::get_if<std::shared_ptr<Series>>(&source_val);
        if (!p) {
            return result_series;
        }
        auto source_series = *p;
        int current_bar = vm.getCurrentBarIndex();
        int offset = static_cast<int>(vm.getNumericValue(offset_val));

        std::string cache_key = "REF(" + source_series->name + "~" + std::to_string(offset) + ")";

        if (vm.builtin_func_cache.count(cache_key)) {
            result_series = vm.builtin_func_cache.at(cache_key);
        } else {
            result_series = std::make_shared<Series>();
            result_series->name = cache_key;
            vm.builtin_func_cache[cache_key] = result_series;
        }

        if (current_bar >= result_series->data.size() || std::isnan(result_series->data[current_bar])) {
            double ref_val = source_series->getCurrent(current_bar - offset);
            result_series->setCurrent(current_bar, ref_val);
        }
        return result_series;
    };
    built_in_funcs["if"] = [](PineVM& vm) -> Value {
        Value false_val = vm.pop();
        Value true_val = vm.pop();
        Value condition_val = vm.pop();

        bool condition = vm.getBoolValue(condition_val);

        if (condition) {
            return true_val;
        } else {
            return false_val;
        }
    };

    built_in_funcs["cross"] = [](PineVM& vm) -> Value {
        Value val2 = vm.pop();
        Value val1 = vm.pop();

        double dval1 = vm.getNumericValue(val1);
        double dval2 = vm.getNumericValue(val2);

        // 获取前一个K线柱的两个序列的值
        double prev_dval1 = std::get<std::shared_ptr<Series>>(val1)->getCurrent(vm.getCurrentBarIndex() - 1);
        double prev_dval2 = std::get<std::shared_ptr<Series>>(val2)->getCurrent(vm.getCurrentBarIndex() - 1);

        // 检查是否有NaN值
        if (std::isnan(dval1) || std::isnan(dval2) || std::isnan(prev_dval1) || std::isnan(prev_dval2)) {
            return false; // 任何一个值为NaN，则交叉不成立
        }

        // 判断是否发生交叉
        // (A > B 且 A[1] <= B[1]) 或 (A < B 且 A[1] >= B[1])
        bool cross_up = (dval1 > dval2) && (prev_dval1 <= prev_dval2);
        //bool cross_down = (dval1 < dval2) && (prev_dval1 >= prev_dval2);

        //return cross_up || cross_down;
        return cross_up;
    };

    built_in_funcs["input.int"] = [](PineVM& vm) -> Value {
         vm.pop();
         Value defval = vm.pop();
         return defval;
    };
}

void PineVM::printPlottedResults() const {
    if (plotted_series.empty()) {
        std::cout << "\n--- No Plotted Results ---" << std::endl;
        return;
    }

    // 查找名为 "time" 的序列
    std::shared_ptr<Series> time_series = nullptr;
    for (const auto& pair : built_in_vars) {
        if (pair.first == "time" && std::holds_alternative<std::shared_ptr<Series>>(pair.second)) {
            time_series = std::get<std::shared_ptr<Series>>(pair.second);
            break;
        }
    }

    if (time_series) {
        std::cout << "\n--- Time Series (前10个和后10个值) ---" << std::endl;
        const auto& data = time_series->data;
        const size_t n = data.size();

        auto print_time_value = [](double val) {
            // 假设时间戳是YYYYMMDD格式的整数，转换为可读的日期
            if (std::isnan(val)) {
                std::cout << "nan";
            } else {
                long long date_int = static_cast<long long>(val);
                std::cout << date_int;
            }
        };

        std::cout << "  Data (total " << n << " points): [";

        if (n <= 20) {
            for (size_t i = 0; i < n; ++i) {
                if (i > 0) std::cout << ", ";
                print_time_value(data[i]);
            }
        } else {
            for (size_t i = 0; i < 10; ++i) {
                if (i > 0) std::cout << ", ";
                print_time_value(data[i]);
            }
            std::cout << ", ...";
            for (size_t i = n - 10; i < n; ++i) {
                std::cout << ", ";
                print_time_value(data[i]);
            }
        }
        std::cout << "]" << std::endl;
    }
    std::cout << "\n--- Plotted Results (前10个和后10个值) ---" << std::endl;
    for (const auto& plotted : plotted_series) {
        std::cout << "Series: " << plotted.series->name << ", Color: " << plotted.color << std::endl;
        
        const auto& data = plotted.series->data;
        const size_t n = data.size();

        auto print_value = [](double val) {
            if (std::isnan(val)) {
                std::cout << "nan";
            } else {
                std::cout << std::fixed << std::setprecision(2) << val;
            }
        };

        std::cout << "  Data (total " << n << " points): [";

        if (n <= 20) {
            // 如果点数少于等于20，则全部打印
            for (size_t i = 0; i < n; ++i) {
                if (i > 0) std::cout << ", ";
                print_value(data[i]);
            }
        } else {
            // 打印前10个点
            for (size_t i = 0; i < 10; ++i) {
                if (i > 0) std::cout << ", ";
                print_value(data[i]);
            }
            std::cout << ", ...";
            // 打印后10个点
            for (size_t i = n - 10; i < n; ++i) {
                std::cout << ", ";
                print_value(data[i]);
            }
        }
        std::cout << "]" << std::endl;
    }
}

// 1. 私有辅助函数 (核心逻辑)
void PineVM::writePlottedResultsToStream(std::ostream& stream) const {
    if (plotted_series.empty()) {
        std::cout << "\n--- No Plotted Results ---" << std::endl;
        return;
    }
    // 查找名为 "time" 的序列
    std::shared_ptr<Series> time_series = nullptr;
    for (const auto& pair : built_in_vars) {
        if (pair.first == "time" && std::holds_alternative<std::shared_ptr<Series>>(pair.second)) {
            time_series = std::get<std::shared_ptr<Series>>(pair.second);
            break;
        }
    }

    // 写入CSV头
    bool first_series = true;
    if (time_series) {
        stream << "time";
        first_series = false;
    }
    for (const auto& plotted : plotted_series) {
        if (!first_series) {
            stream << ",";
        }
        stream << plotted.series->name;
        first_series = false;
    }
    stream << "\n";

    // 写入数据
    if (!plotted_series.empty() || time_series) {
        size_t max_data_points = 0;
        if (time_series) {
            max_data_points = time_series->data.size();
        }
        for (const auto& plotted : plotted_series) {
            if (plotted.series->data.size() > max_data_points) {
                max_data_points = plotted.series->data.size();
            }
        }

        for (size_t i = 0; i < max_data_points; ++i) {
            first_series = true;
            if (time_series) {
                if (i < time_series->data.size()) {
                    // 时间戳通常不需要小数，使用 std::fixed 和 setprecision(0)
                    stream << std::fixed << std::setprecision(0) << time_series->data[i];
                }
                first_series = false;
            }
            for (const auto& plotted : plotted_series) {
                if (!first_series) {
                    stream << ",";
                }
                if (i < plotted.series->data.size()) {
                    // 数据保留两位小数
                    stream << std::fixed << std::setprecision(2) << plotted.series->data[i];
                }
                first_series = false;
            }
            stream << "\n";
        }
    }
}

// 2. 新的公共接口：写入文件
void PineVM::writePlottedResultsToFile(const std::string& filename) const {
    std::ofstream outfile(filename);
    if (!outfile.is_open()) {
        std::cerr << "Error: Could not open file " << filename << " for writing." << std::endl;
        return;
    }

    // 调用核心逻辑函数
    writePlottedResultsToStream(outfile);

    outfile.close(); // ofstream 在析构时会自动关闭，但显式关闭也是好习惯
    std::cout << "Plotted results written to " << filename << std::endl;
}

// 3. 新的公共接口：输出为字符串
std::string PineVM::getPlottedResultsAsString() const {
    std::stringstream ss;

    // 调用核心逻辑函数
    writePlottedResultsToStream(ss);

    // 从 stringstream 获取字符串并返回
    return ss.str();
}

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

std::string PineVM::bytecodeToTxt(const Bytecode& bytecode)
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

Bytecode PineVM::txtToBytecode(const std::string& txt)
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