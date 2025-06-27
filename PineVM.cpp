#include "PineVM.h"
#include "duckdb.h" // 使用 DuckDB C API
#include <iostream>
#include <numeric>

double Series::getCurrent(int bar_index) {
    // 1. 首先检查缓存
    if (bar_index >= 0 && bar_index < data.size()) {
        // 如果值不是 NaN，说明已经缓存或计算过
        if (!std::isnan(data[bar_index])) {
            return data[bar_index];
        }
    }

    // 2. 如果不在缓存中，并且这是一个数据库支持的序列，则从数据库获取
    if (con != nullptr) {
        // 确保向量足够大以容纳新值，并用NAN填充
        if (bar_index >= data.size()) {
            data.resize(bar_index + 1, NAN);
        }

        duckdb_result result;
        std::string query_str = "SELECT " + name + " FROM market_data WHERE bar_id = " + std::to_string(bar_index);
        const char* query_c_str = query_str.c_str();

        // 执行查询
        duckdb_state status = duckdb_query(con, query_c_str, &result);

        if (status != DuckDBSuccess) {
            std::cerr << "DuckDB query error: " << duckdb_result_error(&result) << std::endl;
            duckdb_destroy_result(&result); // 总是销毁结果
            data[bar_index] = NAN; // 缓存失败结果
            return NAN;
        }

        // 检查是否获取到一行数据且该值不为 NULL
        // DuckDB C API 的 duckdb_value_double 会在值为 NULL 时返回 NAN
        if (duckdb_row_count(&result) > 0 && duckdb_column_count(&result) > 0) {
            double value = duckdb_value_double(&result, 0, 0); // 获取第一行第一列的值
            data[bar_index] = value; // 缓存成功获取的值 (包括来自DB的NAN)
            duckdb_destroy_result(&result);
            return value;
        } else {
            // 没有行或列，或者值为空/无效
            data[bar_index] = NAN;
            duckdb_destroy_result(&result);
            return NAN;
        }
    }

    // 3. 如果没有数据库连接或查询失败/无结果，返回当前缓存中的值（可能是NAN）或NAN
    if (bar_index >= 0 && bar_index < data.size()) {
        return data[bar_index];
    }
    return NAN; // 通常只在 bar_index < 0 时触发
}

// ... Series::setCurrent 保持不变 ...
void Series::setCurrent(int bar_index, double value) {
    if (bar_index >= data.size()) {
        data.resize(bar_index + 1, NAN);
    }
    data[bar_index] = value;
}

// PineVM 构造函数现在负责打开 DuckDB 数据库和连接
PineVM::PineVM(int total_bars, const std::string& db_path_str)
    : total_bars(total_bars), bar_index(0), database(nullptr), connection(nullptr) {
    
    const char* db_path = db_path_str.empty() ? nullptr : db_path_str.c_str();

    // 打开数据库
    duckdb_state status_db = duckdb_open(db_path, &database);
    if (status_db != DuckDBSuccess) {
        throw std::runtime_error("Failed to open DuckDB database at " + (db_path ? db_path_str : ":memory:"));
    }

    // 连接到数据库
    duckdb_state status_con = duckdb_connect(database, &connection);
    if (status_con != DuckDBSuccess) {
        duckdb_close(&database); // 如果连接失败，清理数据库
        throw std::runtime_error("Failed to connect to DuckDB database.");
    }
    registerBuiltins();
}

PineVM::~PineVM() {
    if (connection != nullptr) {
        duckdb_disconnect(&connection);
    }
    if (database != nullptr) {
        duckdb_close(&database);
    }
}

void PineVM::loadBytecode(const Bytecode* code) {
    bytecode = code;
}

void PineVM::execute() {
    globals.resize(10); // 假设最多10个全局变量, 在所有K线计算前初始化一次
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
                    // 检查是否是已知的市场数据序列名称
                    // 这些将从数据库中惰性加载
                    if (name == "close" || name == "high" || name == "low" || name == "open") {
                        auto series = std::make_shared<Series>();
                        series->name = name;
                        series->con = connection; // 将序列链接到数据库连接
                        built_in_vars[name] = series;
                        push(series);
                    } else {
                        throw std::runtime_error("Undefined built-in variable: " + name);
                    }
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
                Value plot_name_val = pop(); // Pop plot name (e.g., "Plot1")
                Value color_val = pop();
                Value series_to_plot_val = pop();
                
                auto series_ptr = std::get<std::shared_ptr<Series>>(series_to_plot_val);
                double current_val = series_ptr->getCurrent(bar_index);
                
                std::string plot_name_str = std::get<std::string>(plot_name_val);
                std::string color_str = "default_color";
                if (std::holds_alternative<std::string>(color_val)) {
                    // The compiler pushes color constants as strings like "color.blue"
                    color_str = std::get<std::string>(color_val);
                }

                std::cout << "PLOT> " << plot_name_str << " plotting '" << series_ptr->name << "' at bar " << bar_index
                          << " with value: " << current_val << " and color: " << color_str << std::endl;

                push(true);
                break;
            }
            default:
                throw std::runtime_error("Unknown opcode!");
        }
        ip++;
    }
}

duckdb_connection PineVM::getConnection() {
    return connection;
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