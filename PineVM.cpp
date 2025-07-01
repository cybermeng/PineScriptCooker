#include "PineVM.h"
#include <iostream>
#include <iomanip>
#include <numeric>
#include <algorithm> // For std::find_if

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

// PineVM 构造函数现在负责打开 DuckDB 数据库和连接
PineVM::PineVM(int total_bars)
    : total_bars(total_bars), bar_index(0), bytecode(nullptr), ip(nullptr) {
    registerBuiltins();
}

PineVM::~PineVM() {
}

void PineVM::loadBytecode(const Bytecode* code) {
    bytecode = code;
}

void PineVM::execute() {
    globals.resize(10); // 假设最多10个全局变量, 在所有K线计算前初始化一次
    plotted_series.clear(); // 在每次执行前清除之前的绘制结果
    for (bar_index = 0; bar_index < total_bars; ++bar_index) {
        // std::cout << "--- Executing Bar #" << bar_index << " ---" << std::endl; // 注释掉以保持输出整洁
        runCurrentBar();
    }
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
    } else if (auto* p = std::get_if<std::shared_ptr<Series>>(&val)) {
        return (*p)->getCurrent(bar_index);
    } else {
        throw std::runtime_error("Unsupported operand type for numeric operation.");
    }
}

void PineVM::runCurrentBar() {
    ip = &bytecode->instructions[0];

    while (ip->op != OpCode::HALT) {
        switch (ip->op) {
            case OpCode::PUSH_CONST: {
                push(bytecode->constant_pool[ip->operand]);
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
            case OpCode::GREATER_EQUAL: {
                double right = getNumericValue(pop());
                double left = getNumericValue(pop());
                if (ip->op == OpCode::ADD) push(left + right);
                else if (ip->op == OpCode::DIV) {
                    if (right == 0.0) {
                        push(NAN);
                    } else push(left / right);
                }
                else if (ip->op == OpCode::SUB) push(left - right);
                else if (ip->op == OpCode::MUL) push(left * right);
                else if (ip->op == OpCode::DIV) push(left / right);
                else if (ip->op == OpCode::LESS) push(left < right);
                else if (ip->op == OpCode::LESS_EQUAL) push(left <= right);
                else if (ip->op == OpCode::EQUAL_EQUAL) push(left == right);
                else if (ip->op == OpCode::BANG_EQUAL) push(left != right);
                else if (ip->op == OpCode::GREATER) push(left > right);
                else if (ip->op == OpCode::GREATER_EQUAL) push(left >= right);
                break;
                }
            case OpCode::LOAD_GLOBAL: {
                push(globals[ip->operand]);
                break;
            }
            case OpCode::STORE_GLOBAL: {
                globals[ip->operand] = pop();
                break;
            }
            case OpCode::LOAD_BUILTIN_VAR: {
                const std::string& name = std::get<std::string>(bytecode->constant_pool[ip->operand]);
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
                const std::string& func_name = std::get<std::string>(bytecode->constant_pool[ip->operand]);
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
        
        int length = static_cast<int>(std::get<double>(length_val));
        auto source_series = std::get<std::shared_ptr<Series>>(source_val);

        // 基于函数和参数创建唯一的缓存键，以支持状态保持
        std::string cache_key = "ta.sma(" + source_series->name + "," + std::to_string(length) + ")";

        std::shared_ptr<Series> result_series;
        if (vm.builtin_func_cache.count(cache_key)) {
            result_series = vm.builtin_func_cache.at(cache_key);
        } else {
            result_series = std::make_shared<Series>();
            result_series->name = "sma(" + source_series->name + ", " + std::to_string(length) + ")";
            vm.builtin_func_cache[cache_key] = result_series;
        }
        
        int current_bar = vm.getCurrentBarIndex();

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
    
    built_in_funcs["ta.ema"] = [](PineVM& vm) -> Value {
        Value length_val = vm.pop();
        Value source_val = vm.pop();
        
        int length = static_cast<int>(std::get<double>(length_val));
        auto source_series = std::get<std::shared_ptr<Series>>(source_val);

        std::string cache_key = "ta.ema(" + source_series->name + "," + std::to_string(length) + ")";

        std::shared_ptr<Series> result_series;
        if (vm.builtin_func_cache.count(cache_key)) {
            result_series = vm.builtin_func_cache.at(cache_key);
        } else {
            result_series = std::make_shared<Series>();
            result_series->name = "ema(" + source_series->name + ", " + std::to_string(length) + ")";
            vm.builtin_func_cache[cache_key] = result_series;
        }
        
        int current_bar = vm.getCurrentBarIndex();

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

        int length = static_cast<int>(std::get<double>(length_val));
        auto source_series = std::get<std::shared_ptr<Series>>(source_val);

        std::string src_name = source_series->name;
        std::string rsi_key = "ta.rsi(" + src_name + "," + std::to_string(length) + ")";
        std::string gain_key = "__rsi_avg_gain(" + src_name + "," + std::to_string(length) + ")";
        std::string loss_key = "__rsi_avg_loss(" + src_name + "," + std::to_string(length) + ")";

        auto get_or_create = [&](const std::string& key, const std::string& name) {
            if (vm.builtin_func_cache.count(key)) return vm.builtin_func_cache.at(key);
            auto series = std::make_shared<Series>();
            series->name = name;
            vm.builtin_func_cache[key] = series;
            return series;
        };

        auto rsi_series = get_or_create(rsi_key, "rsi(" + src_name + ", " + std::to_string(length) + ")");
        auto gain_series = get_or_create(gain_key, gain_key);
        auto loss_series = get_or_create(loss_key, loss_key);

        int current_bar = vm.getCurrentBarIndex();
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