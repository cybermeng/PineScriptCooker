#include "PineVM.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <numeric>
#include <random>
#include <chrono>

void PineVM::registerBuiltinsHithink()
{ 
    /////////////////////////////////////////////////////////////////////////////////////////////
    //引用函数
    built_in_funcs["ama"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: X (series), A (numeric)
            auto source_series = ctx.getArgAsSeries(0);
            double alpha = ctx.getArgAsNumeric(1);

            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();

            double current_source_val = source_series->getCurrent(current_bar);
            double prev_ama = result_series->getCurrent(current_bar - 1);

            double ama_val;
            if (std::isnan(current_source_val)) {
                ama_val = NAN;
            } else if (std::isnan(prev_ama)) {
                ama_val = current_source_val;
            } else {
                ama_val = prev_ama + alpha * (current_source_val - prev_ama);
            }
            result_series->setCurrent(current_bar, ama_val);
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };

    built_in_funcs["barscount"] = {
        .function = [](FunctionContext &ctx) -> Value {
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            // BARSCOUNT 有效数据周期数
            // 有效数据周期数.
            // 用法:
            // BARSCOUNT(X)第一个有效数据到当前的间隔周期数
            auto source_series = ctx.getArgAsSeries(0);

            int count = 0;
            for (int i = 0; i <= current_bar; ++i) {
                double val = source_series->getCurrent(i);
                if (!std::isnan(val)) {
                    count++;
                }
            }
            result_series->setCurrent(current_bar, static_cast<double>(count));
            return result_series;
        },
        .min_args = 1,
        .max_args = 1
    };

    built_in_funcs["barslast"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: X (series)
            auto condition_series = ctx.getArgAsSeries(0);
            
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();

            double barslast_val = NAN;
            for (int i = 0; i <= current_bar; ++i) {
                double val = condition_series->getCurrent(current_bar - i);
                if (!std::isnan(val) && val != 0.0) {
                    barslast_val = static_cast<double>(i);
                    break;
                }
            }
            result_series->setCurrent(current_bar, barslast_val);
            return result_series;
        },
        .min_args = 1,
        .max_args = 1
    };

    built_in_funcs["barslastcount"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: X (series)
            auto condition_series = ctx.getArgAsSeries(0);
            
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();

            int count = 0;
            for (int i = current_bar; i >= 0; --i) {
                double val = condition_series->getCurrent(i);
                if (!std::isnan(val) && val != 0.0) {
                    count++;
                } else {
                    break;
                }
            }
            result_series->setCurrent(current_bar, static_cast<double>(count));
            return result_series;
        },
        .min_args = 1,
        .max_args = 1
    };

    built_in_funcs["barssince"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: X (series)
            auto condition_series = ctx.getArgAsSeries(0);

            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();

            int bars_since = -1; // -1 表示从未发生
            for (int i = 0; i <= current_bar; ++i) {
                double val = condition_series->getCurrent(current_bar - i);
                if (!std::isnan(val) && val != 0.0) {
                    bars_since = i;
                    break;
                }
            }
            result_series->setCurrent(current_bar, static_cast<double>(bars_since));
            return result_series;
        },
        .min_args = 1,
        .max_args = 1
    };

    built_in_funcs["barssincen"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: X (series), N (numeric)
            auto condition_series = ctx.getArgAsSeries(0);
            int length = static_cast<int>(ctx.getArgAsNumeric(1));

            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();

            int bars_since = -1;
            int count = 0;
            for (int i = 0; i <= current_bar; ++i) {
                double val = condition_series->getCurrent(current_bar - i);
                if (!std::isnan(val) && val != 0.0) {
                    bars_since = i;
                    count++;
                    if (count >= length) {
                        break;
                    }
                }
            }
            result_series->setCurrent(current_bar, static_cast<double>(bars_since));
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };

    built_in_funcs["barsstatus"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: COND (series)
            auto condition_series = ctx.getArgAsSeries(0);
            
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            
            int count = 0;
            for (int i = current_bar; i >= 0; --i) {
                double val = condition_series->getCurrent(i);
                if (!std::isnan(val) && val != 0.0) {
                    count++;
                } else {
                    break;
                }
            }
            result_series->setCurrent(current_bar, static_cast<double>(count));
            return result_series;
        },
        .min_args = 1,
        .max_args = 1
    };
    
    built_in_funcs["const"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: X (numeric)
            double dval = ctx.getArgAsNumeric(0);
            
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            
            result_series->setCurrent(current_bar, dval);
            return result_series;
        },
        .min_args = 1,
        .max_args = 1
    };

    built_in_funcs["count"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: X (series), N (numeric)
            auto condition_series = ctx.getArgAsSeries(0);
            int length = static_cast<int>(ctx.getArgAsNumeric(1));
            
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            
            int count = 0;
            for (int i = 0; i < length && current_bar - i >= 0; ++i) {
                double val = condition_series->getCurrent(current_bar - i);
                if (!std::isnan(val) && val != 0.0) {
                    count++;
                }
            }
            result_series->setCurrent(current_bar, static_cast<double>(count));
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };
    
    built_in_funcs["currbarscount"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // TODO
            return ctx.getResultSeries();
        },
        .min_args = 0, // Placeholder
        .max_args = 0  // Placeholder
    };

    built_in_funcs["dma"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: X (series), A (numeric)
            auto source_series = ctx.getArgAsSeries(0);
            double alpha = ctx.getArgAsNumeric(1);

            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            
            double current_source_val = source_series->getCurrent(current_bar);
            double prev_dma = result_series->getCurrent(current_bar - 1);

            double dma_val;
            if (std::isnan(current_source_val)) {
                dma_val = NAN;
            } else if (std::isnan(prev_dma)) {
                dma_val = current_source_val;
            } else {
                dma_val = alpha * current_source_val + (1.0 - alpha) * prev_dma;
            }
            result_series->setCurrent(current_bar, dma_val);
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };

    built_in_funcs["ema"] = built_in_funcs["expma"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: X (series), N (numeric)
            auto source_series = ctx.getArgAsSeries(0);
            int length = static_cast<int>(ctx.getArgAsNumeric(1));

            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            
            double current_source_val = source_series->getCurrent(current_bar);
            double prev_ema = result_series->getCurrent(current_bar - 1);

            double ema_val;
            if (std::isnan(current_source_val)) {
                ema_val = NAN;
            } else if (std::isnan(prev_ema)) {
                ema_val = current_source_val;
            } else {
                ema_val = (current_source_val * 2 + prev_ema * (length - 1)) / (length + 1);
            }
            result_series->setCurrent(current_bar, ema_val);
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };

    built_in_funcs["expmema"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: X (series), N (numeric)
            auto source_series = ctx.getArgAsSeries(0);
            int length = static_cast<int>(ctx.getArgAsNumeric(1));

            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();

            double expmema_val;
            if (current_bar < length - 1) {
                expmema_val = NAN;
            } else {
                double current_source_val = source_series->getCurrent(current_bar);
                double prev_expmema = result_series->getCurrent(current_bar - 1);

                if (std::isnan(current_source_val)) {
                    expmema_val = NAN;
                } else if (std::isnan(prev_expmema)) {
                    double sum = 0.0;
                    int count = 0;
                    for (int i = 0; i < length; ++i) {
                        double val = source_series->getCurrent(current_bar - i);
                        if (!std::isnan(val)) {
                            sum += val;
                            count++;
                        }
                    }
                    expmema_val = (count == length) ? sum / count : NAN;
                } else {
                    expmema_val = (current_source_val * 2 + prev_expmema * (length - 1)) / (length + 1);
                }
            }
            result_series->setCurrent(current_bar, expmema_val);
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };

    built_in_funcs["filter"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: COND (series), N (numeric)
            auto condition_series = ctx.getArgAsSeries(0);
            int length = static_cast<int>(ctx.getArgAsNumeric(1));
            
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            
            bool any_true = false;
            for (int i = 1; i < length && current_bar - i >= 0; ++i) {
                double val = result_series->getCurrent(current_bar - i);
                if (!std::isnan(val) && val != 0.0) {
                    any_true = true;
                    break;
                }
            }
            if (any_true)
                result_series->setCurrent(current_bar, static_cast<double>(false));
            else
                result_series->setCurrent(current_bar, condition_series->getCurrent(current_bar));
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };

    built_in_funcs["findhigh"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: VAR (series), N, M, T (numeric)
            auto var_series = ctx.getArgAsSeries(0);
            int N = static_cast<int>(ctx.getArgAsNumeric(1));
            int M = static_cast<int>(ctx.getArgAsNumeric(2));
            int T = static_cast<int>(ctx.getArgAsNumeric(3));

            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();

            int start_idx = current_bar - N - M + 1;
            int end_idx = current_bar - N;
            if (start_idx < 0) start_idx = 0;

            std::vector<double> values_in_range;
            for (int i = start_idx; i <= end_idx; ++i) {
                if (i >= 0) {
                    double val = var_series->getCurrent(i);
                    if (!std::isnan(val)) values_in_range.push_back(val);
                }
            }

            if (values_in_range.empty() || T <= 0 || T > values_in_range.size()) {
                result_series->setCurrent(current_bar, NAN);
            } else {
                std::sort(values_in_range.rbegin(), values_in_range.rend());
                result_series->setCurrent(current_bar, values_in_range[T - 1]);
            }
            return result_series;
        },
        .min_args = 4,
        .max_args = 4
    };

    built_in_funcs["findhighbars"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: VAR (series), N, M, T (numeric)
            auto var_series = ctx.getArgAsSeries(0);
            int N = static_cast<int>(ctx.getArgAsNumeric(1));
            int M = static_cast<int>(ctx.getArgAsNumeric(2));
            int T = static_cast<int>(ctx.getArgAsNumeric(3));
            
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();

            int start_idx = current_bar - N - M + 1;
            int end_idx = current_bar - N;
            if (start_idx < 0) start_idx = 0;

            std::vector<std::pair<double, int>> values_with_indices;
            for (int i = start_idx; i <= end_idx; ++i) {
                if (i >= 0) {
                    double val = var_series->getCurrent(i);
                    if (!std::isnan(val)) values_with_indices.push_back({val, i});
                }
            }

            if (values_with_indices.empty() || T <= 0 || T > values_with_indices.size()) {
                result_series->setCurrent(current_bar, NAN);
            } else {
                std::sort(values_with_indices.begin(), values_with_indices.end(),
                          [](const auto &a, const auto &b) {
                              if (a.first != b.first) return a.first > b.first;
                              return a.second < b.second;
                          });
                int target_original_idx = values_with_indices[T - 1].second;
                result_series->setCurrent(current_bar, static_cast<double>(current_bar - target_original_idx));
            }
            return result_series;
        },
        .min_args = 4,
        .max_args = 4
    };
    
    built_in_funcs["findlow"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: VAR (series), N, M, T (numeric)
            auto var_series = ctx.getArgAsSeries(0);
            int N = static_cast<int>(ctx.getArgAsNumeric(1));
            int M = static_cast<int>(ctx.getArgAsNumeric(2));
            int T = static_cast<int>(ctx.getArgAsNumeric(3));

            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            
            int start_idx = current_bar - N - M + 1;
            int end_idx = current_bar - N;
            if (start_idx < 0) start_idx = 0;

            std::vector<double> values_in_range;
            for (int i = start_idx; i <= end_idx; ++i) {
                if (i >= 0) {
                    double val = var_series->getCurrent(i);
                    if (!std::isnan(val)) values_in_range.push_back(val);
                }
            }

            if (values_in_range.empty() || T <= 0 || T > values_in_range.size()) {
                result_series->setCurrent(current_bar, NAN);
            } else {
                std::sort(values_in_range.begin(), values_in_range.end());
                result_series->setCurrent(current_bar, values_in_range[T - 1]);
            }
            return result_series;
        },
        .min_args = 4,
        .max_args = 4
    };

    built_in_funcs["findlowbars"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: VAR (series), N, M, T (numeric)
            auto var_series = ctx.getArgAsSeries(0);
            int N = static_cast<int>(ctx.getArgAsNumeric(1));
            int M = static_cast<int>(ctx.getArgAsNumeric(2));
            int T = static_cast<int>(ctx.getArgAsNumeric(3));
            
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();

            int start_idx = current_bar - N - M + 1;
            int end_idx = current_bar - N;
            if (start_idx < 0) start_idx = 0;

            std::vector<std::pair<double, int>> values_with_indices;
            for (int i = start_idx; i <= end_idx; ++i) {
                if (i >= 0) {
                    double val = var_series->getCurrent(i);
                    if (!std::isnan(val)) values_with_indices.push_back({val, i});
                }
            }

            if (values_with_indices.empty() || T <= 0 || T > values_with_indices.size()) {
                result_series->setCurrent(current_bar, NAN);
            } else {
                std::sort(values_with_indices.begin(), values_with_indices.end(),
                          [](const auto &a, const auto &b) {
                              if (a.first != b.first) return a.first < b.first;
                              return a.second < b.second;
                          });
                int target_original_idx = values_with_indices[T - 1].second;
                result_series->setCurrent(current_bar, static_cast<double>(current_bar - target_original_idx));
            }
            return result_series;
        },
        .min_args = 4,
        .max_args = 4
    };

    built_in_funcs["hhv"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: source (series), length (numeric)
            auto source_series = ctx.getArgAsSeries(0);
            int length = static_cast<int>(ctx.getArgAsNumeric(1));

            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            
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
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };

    built_in_funcs["hv"] = {
        .function = [](FunctionContext &ctx) -> Value {
            auto source_series = ctx.getArgAsSeries(0);
            int length = static_cast<int>(ctx.getArgAsNumeric(1));
            
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            
            double highest_val = NAN;
            bool first = true;
            for (int i = 1; i < length && current_bar - i >= 0; ++i) {
                double val = source_series->getCurrent(current_bar - i);
                if (!std::isnan(val)) {
                    if (first) { highest_val = val; first = false; }
                    else { highest_val = std::max(highest_val, val); }
                }
            }
            result_series->setCurrent(current_bar, highest_val);
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };
    
    built_in_funcs["hhvbars"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: source (series), length (numeric)
            auto source_series = ctx.getArgAsSeries(0);
            int length = static_cast<int>(ctx.getArgAsNumeric(1));
            
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            
            double highest_val = NAN;
            int highest_idx = -1;
            bool first = true;
            for (int i = 0; i < length && current_bar - i >= 0; ++i) {
                double val = source_series->getCurrent(current_bar - i);
                if (!std::isnan(val)) {
                    if (first) {
                        highest_val = val;
                        highest_idx = i;
                        first = false;
                    } else if (val >= highest_val) {
                        highest_val = val;
                        highest_idx = i;
                    }
                }
            }
            result_series->setCurrent(current_bar, static_cast<double>(highest_idx));
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };

    built_in_funcs["hod"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: source (series), offset (numeric)
            auto source_series = ctx.getArgAsSeries(0);
            int offset = static_cast<int>(ctx.getArgAsNumeric(1));
            
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            
            double hod_val = source_series->getCurrent(current_bar - offset);
            result_series->setCurrent(current_bar, hod_val);
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };
    
    built_in_funcs["islastbar"] = {
        .function = [](FunctionContext &ctx) -> Value {
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            int total_bars = ctx.getVM().getTotalBars();

            result_series->setCurrent(current_bar, static_cast<double>(current_bar == total_bars - 1));
            return result_series;
        },
        .min_args = 0,
        .max_args = 0
    };
    
    built_in_funcs["llv"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: source (series), length (numeric)
            auto source_series = ctx.getArgAsSeries(0);
            int length = static_cast<int>(ctx.getArgAsNumeric(1));

            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            
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
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };

    built_in_funcs["lv"] = {
        .function = [](FunctionContext &ctx) -> Value {
            auto source_series = ctx.getArgAsSeries(0);
            int length = static_cast<int>(ctx.getArgAsNumeric(1));
            
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            
            double lowest_val = NAN;
            bool first = true;
            for (int i = 1; i < length && current_bar - i >= 0; ++i) {
                double val = source_series->getCurrent(current_bar - i);
                if (!std::isnan(val)) {
                    if (first) { lowest_val = val; first = false; }
                    else { lowest_val = std::min(lowest_val, val); }
                }
            }
            result_series->setCurrent(current_bar, lowest_val);
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };

    built_in_funcs["llvbars"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: source (series), length (numeric)
            auto source_series = ctx.getArgAsSeries(0);
            int length = static_cast<int>(ctx.getArgAsNumeric(1));

            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();

            double lowest_val = NAN;
            int lowest_idx = -1;
            bool first = true;
            for (int i = 0; i < length && current_bar - i >= 0; ++i) {
                double val = source_series->getCurrent(current_bar - i);
                if (!std::isnan(val)) {
                    if (first) {
                        lowest_val = val;
                        lowest_idx = i;
                        first = false;
                    } else if (val <= lowest_val) {
                        lowest_val = val;
                        lowest_idx = i;
                    }
                }
            }
            result_series->setCurrent(current_bar, static_cast<double>(lowest_idx));
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };
    
    built_in_funcs["lod"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: source (series), offset (numeric)
            auto source_series = ctx.getArgAsSeries(0);
            int offset = static_cast<int>(ctx.getArgAsNumeric(1));
            
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            
            double lod_val = source_series->getCurrent(current_bar - offset);
            result_series->setCurrent(current_bar, lod_val);
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };
    
    built_in_funcs["lowrange"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: source (series), offset (numeric)
            auto source_series = ctx.getArgAsSeries(0);
            int offset = static_cast<int>(ctx.getArgAsNumeric(1));
            
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            
            double low_val = source_series->getCurrent(current_bar - offset);
            result_series->setCurrent(current_bar, low_val);
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };

    built_in_funcs["ma"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: source (series), length (numeric)
            auto source_series = ctx.getArgAsSeries(0);
            int length = static_cast<int>(ctx.getArgAsNumeric(1));

            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();

            double sum = 0.0;
            int count = 0;
            for (int i = 0; i < length && current_bar - i >= 0; ++i) {
                double val = source_series->getCurrent(current_bar - i);
                if (!std::isnan(val)) {
                    sum += val;
                    count++;
                }
            }
            double ma_val = (count == length) ? sum / count : NAN;
            result_series->setCurrent(current_bar, ma_val);
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };
    
    built_in_funcs["mema"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: source (series), length (numeric)
            auto source_series = ctx.getArgAsSeries(0);
            int length = static_cast<int>(ctx.getArgAsNumeric(1));

            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();

            double current_source_val = source_series->getCurrent(current_bar);
            double prev_mema = result_series->getCurrent(current_bar - 1);

            double mema_val;
            if (std::isnan(current_source_val)) {
                mema_val = NAN;
            } else if (std::isnan(prev_mema)) {
                // First valid MEMA is typically an SMA of the period
                double sum = 0.0;
                int count = 0;
                for (int i = 0; i < length && current_bar - i >= 0; ++i) {
                    double val = source_series->getCurrent(current_bar - i);
                    if(!std::isnan(val)) { sum += val; count++; }
                }
                mema_val = (count == length) ? sum / length : NAN;
            } else {
                mema_val = (current_source_val + prev_mema * (length - 1)) / length;
            }
            result_series->setCurrent(current_bar, mema_val);
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };

    built_in_funcs["mular"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: source (series), length (numeric)
            auto source_series = ctx.getArgAsSeries(0);
            int length = static_cast<int>(ctx.getArgAsNumeric(1));

            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();

            double product = 1.0;
            bool has_nan = false;
            int start_bar = (length == 0) ? 0 : (current_bar - length + 1);

            for (int i = start_bar; i <= current_bar; ++i) {
                if (i < 0) { has_nan = true; break; }
                double val = source_series->getCurrent(i);
                if (std::isnan(val)) { has_nan = true; break; }
                product *= val;
            }

            result_series->setCurrent(current_bar, has_nan ? NAN : product);
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };

    built_in_funcs["range"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: A, B, C (numeric)
            double A = ctx.getArgAsNumeric(0);
            double B = ctx.getArgAsNumeric(1);
            double C = ctx.getArgAsNumeric(2);

            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            
            double range_val = (A > B && A < C) ? 1.0 : 0.0;
            result_series->setCurrent(current_bar, range_val);
            return result_series;
        },
        .min_args = 3,
        .max_args = 3
    };

    built_in_funcs["ref"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: source (series), offset (numeric)
            auto source_series = ctx.getArgAsSeries(0);
            int offset = static_cast<int>(ctx.getArgAsNumeric(1));

            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();

            double ref_val = source_series->getCurrent(current_bar - offset);
            result_series->setCurrent(current_bar, ref_val);
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };

    built_in_funcs["refdate"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // TODO
            ctx.getResultSeries()->setCurrent(ctx.getCurrentBarIndex(), NAN);
            return ctx.getResultSeries();
        },
        .min_args = 2, // Placeholder
        .max_args = 2
    };

    built_in_funcs["refv"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: source (series), offset (numeric)
            auto source_series = ctx.getArgAsSeries(0);
            int offset = static_cast<int>(ctx.getArgAsNumeric(1));
            
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            
            double ref_val = source_series->getCurrent(current_bar - offset);
            result_series->setCurrent(current_bar, ref_val);
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };

    built_in_funcs["reverse"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: source (series)
            auto source_series = ctx.getArgAsSeries(0);
            
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            
            double reversed_val = source_series->getCurrent(current_bar);
            result_series->setCurrent(current_bar, reversed_val);
            return result_series;
        },
        .min_args = 1,
        .max_args = 1
    };

    built_in_funcs["sma"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: X (series), N (numeric), M (numeric)
            auto source_series = ctx.getArgAsSeries(0);
            int length = static_cast<int>(ctx.getArgAsNumeric(1));
            // double weight = ctx.getArgAsNumeric(2); // weight is ignored in original implementation
            
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();

            double sum = 0.0;
            int count = 0;
            for (int i = 0; i < length && current_bar - i >= 0; ++i) {
                double val = source_series->getCurrent(current_bar - i);
                if (!std::isnan(val)) {
                    sum += val;
                    count++;
                }
            }
            double sma_val = (count == length) ? sum / count : NAN;
            result_series->setCurrent(current_bar, sma_val);
            return result_series;
        },
        .min_args = 3,
        .max_args = 3
    };

    built_in_funcs["sum"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: source (series), length (numeric)
            auto source_series = ctx.getArgAsSeries(0);
            int length = static_cast<int>(ctx.getArgAsNumeric(1));

            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();

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
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };
    
    built_in_funcs["sumbars"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: source (series), length (numeric)
            auto source_series = ctx.getArgAsSeries(0);
            int length = static_cast<int>(ctx.getArgAsNumeric(1));

            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            
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
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };

    built_in_funcs["tfilt"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: COND (series), N (numeric)
            auto condition_series = ctx.getArgAsSeries(0);
            int length = static_cast<int>(ctx.getArgAsNumeric(1));
            
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            
            bool all_true = true;
            for (int i = 0; i < length && current_bar - i >= 0; ++i) {
                double val = condition_series->getCurrent(current_bar - i);
                if (std::isnan(val) || val == 0.0) {
                    all_true = false;
                    break;
                }
            }
            result_series->setCurrent(current_bar, static_cast<double>(all_true));
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };
    
    built_in_funcs["tfilter"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: COND (series), N (numeric)
            auto condition_series = ctx.getArgAsSeries(0);
            int length = static_cast<int>(ctx.getArgAsNumeric(1));
            
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();

            bool any_true = false;
            for (int i = 0; i < length && current_bar - i >= 0; ++i) {
                double val = condition_series->getCurrent(current_bar - i);
                if (!std::isnan(val) && val != 0.0) {
                    any_true = true;
                    break;
                }
            }
            result_series->setCurrent(current_bar, static_cast<double>(any_true));
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };
    
    built_in_funcs["tma"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: source (series), length (numeric)
            auto source_series = ctx.getArgAsSeries(0);
            int length = static_cast<int>(ctx.getArgAsNumeric(1));
            
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            
            double tma_val = NAN;
            if (current_bar >= length * 2 - 2) { // Need enough data for SMA of SMA
                double sum_sma = 0.0;
                int sma_count = 0;
                for (int i = 0; i < length; ++i) {
                    double inner_sum = 0.0;
                    int inner_count = 0;
                    for (int j = 0; j < length; ++j) {
                        double val = source_series->getCurrent(current_bar - i - j);
                        if (!std::isnan(val)) {
                            inner_sum += val;
                            inner_count++;
                        }
                    }
                    if (inner_count == length) {
                        sum_sma += (inner_sum / length);
                        sma_count++;
                    } else {
                        sum_sma = NAN;
                        break;
                    }
                }
                if (sma_count == length) {
                    tma_val = sum_sma / length;
                }
            }
            result_series->setCurrent(current_bar, tma_val);
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };
    
    built_in_funcs["totalrange"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // TODO
            return ctx.getResultSeries();
        },
        .min_args = 0, // Placeholder
        .max_args = 0
    };
    
    built_in_funcs["totalbarscount"] = {
        .function = [](FunctionContext &ctx) -> Value {
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            int total_bars = ctx.getVM().getTotalBars();

            result_series->setCurrent(current_bar, static_cast<double>(total_bars));
            return result_series;
        },
        .min_args = 0,
        .max_args = 0
    };
    
    built_in_funcs["wma"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: source (series), length (numeric)
            auto source_series = ctx.getArgAsSeries(0);
            int length = static_cast<int>(ctx.getArgAsNumeric(1));
            
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            
            double wma_val = NAN;
            if (current_bar >= length - 1) {
                double sum_weighted_values = 0.0;
                double sum_weights = 0.0;
                bool has_nan = false;
                for (int i = 0; i < length; ++i) {
                    double val = source_series->getCurrent(current_bar - i);
                    if (std::isnan(val)) { has_nan = true; break; }
                    double weight = length - i;
                    sum_weighted_values += val * weight;
                    sum_weights += weight;
                }
                if (!has_nan && sum_weights > 0) {
                    wma_val = sum_weighted_values / sum_weights;
                }
            }
            result_series->setCurrent(current_bar, wma_val);
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };
    
    built_in_funcs["xma"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: source (series), length (numeric)
            auto source_series = ctx.getArgAsSeries(0);
            int length = static_cast<int>(ctx.getArgAsNumeric(1));
            
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();

            double current_source_val = source_series->getCurrent(current_bar);
            double prev_xma = result_series->getCurrent(current_bar - 1);

            double xma_val;
            if (std::isnan(current_source_val)) {
                xma_val = NAN;
            } else if (std::isnan(prev_xma)) {
                 xma_val = current_source_val; // Seed with current value
            } else {
                xma_val = (current_source_val + prev_xma * (length - 1)) / length;
            }
            result_series->setCurrent(current_bar, xma_val);
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };
    
    // ... (rest of the functions follow the same pattern)
    
    //形态函数
    built_in_funcs["cost"] = {
        .function = [](FunctionContext &ctx) -> Value {
            // Args: (optional) X (numeric)
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            PineVM& vm = ctx.getVM();

            if (ctx.argCount() > 0) {
                result_series->setCurrent(current_bar, ctx.getArgAsNumeric(0));
            } else {
                if (vm.built_in_vars.count("close")) {
                   auto close_series = std::get<std::shared_ptr<Series>>(vm.built_in_vars.at("close"));
                   result_series->setCurrent(current_bar, close_series->getCurrent(current_bar));
                } else {
                   result_series->setCurrent(current_bar, NAN);
                }
            }
            return result_series;
        },
        .min_args = 0,
        .max_args = 1
    };
    built_in_funcs["costex"] = { .function = [](FunctionContext &ctx) { return ctx.getResultSeries(); }, .min_args = 0, .max_args = 2 }; // placeholder
    built_in_funcs["lfs"] = { .function = [](FunctionContext &ctx) { return ctx.getResultSeries(); }, .min_args = 0, .max_args = 0 }; // placeholder
    built_in_funcs["lwinner"] = { .function = [](FunctionContext &ctx) { return ctx.getResultSeries(); }, .min_args = 0, .max_args = 1 }; // placeholder
    built_in_funcs["newsar"] = { .function = [](FunctionContext &ctx) { return ctx.getResultSeries(); }, .min_args = 0, .max_args = 4 }; // placeholder
    built_in_funcs["ppart"] = { .function = [](FunctionContext &ctx) { return ctx.getResultSeries(); }, .min_args = 0, .max_args = 1 }; // placeholder
    built_in_funcs["pwinner"] = { .function = [](FunctionContext &ctx) { return ctx.getResultSeries(); }, .min_args = 0, .max_args = 1 }; // placeholder
    built_in_funcs["sar"] = { .function = [](FunctionContext &ctx) { return ctx.getResultSeries(); }, .min_args = 0, .max_args = 4 }; // placeholder
    built_in_funcs["sarturn"] = { .function = [](FunctionContext &ctx) { return ctx.getResultSeries(); }, .min_args = 0, .max_args = 4 }; // placeholder
    built_in_funcs["winner"] = { .function = [](FunctionContext &ctx) { return ctx.getResultSeries(); }, .min_args = 0, .max_args = 1 }; // placeholder

    //数学函数
    built_in_funcs["abs"] = {
        .function = [](FunctionContext &ctx) -> Value {
            double dval = ctx.getArgAsNumeric(0);
            ctx.getResultSeries()->setCurrent(ctx.getCurrentBarIndex(), std::abs(dval));
            return ctx.getResultSeries();
        },
        .min_args = 1,
        .max_args = 1
    };
    built_in_funcs["acos"] = {
        .function = [](FunctionContext &ctx) -> Value {
            double dval = ctx.getArgAsNumeric(0);
            ctx.getResultSeries()->setCurrent(ctx.getCurrentBarIndex(), std::acos(dval));
            return ctx.getResultSeries();
        },
        .min_args = 1,
        .max_args = 1
    };
    built_in_funcs["asin"] = {
        .function = [](FunctionContext &ctx) -> Value {
            double dval = ctx.getArgAsNumeric(0);
            ctx.getResultSeries()->setCurrent(ctx.getCurrentBarIndex(), std::asin(dval));
            return ctx.getResultSeries();
        },
        .min_args = 1,
        .max_args = 1
    };
    built_in_funcs["atan"] = {
        .function = [](FunctionContext &ctx) -> Value {
            double dval = ctx.getArgAsNumeric(0);
            ctx.getResultSeries()->setCurrent(ctx.getCurrentBarIndex(), std::atan(dval));
            return ctx.getResultSeries();
        },
        .min_args = 1,
        .max_args = 1
    };
    built_in_funcs["between"] = {
        .function = [](FunctionContext &ctx) -> Value {
            double source = ctx.getArgAsNumeric(0);
            double low = ctx.getArgAsNumeric(1);
            double high = ctx.getArgAsNumeric(2);
            double result = (std::isnan(source) || std::isnan(low) || std::isnan(high))
                              ? NAN : static_cast<double>(source >= low && source <= high);
            ctx.getResultSeries()->setCurrent(ctx.getCurrentBarIndex(), result);
            return ctx.getResultSeries();
        },
        .min_args = 3,
        .max_args = 3
    };
    built_in_funcs["ceiling"] = built_in_funcs["ceil"] = {
        .function = [](FunctionContext &ctx) -> Value {
            double dval = ctx.getArgAsNumeric(0);
            ctx.getResultSeries()->setCurrent(ctx.getCurrentBarIndex(), std::ceil(dval));
            return ctx.getResultSeries();
        },
        .min_args = 1,
        .max_args = 1
    };
    built_in_funcs["cos"] = {
        .function = [](FunctionContext &ctx) -> Value {
            double dval = ctx.getArgAsNumeric(0);
            ctx.getResultSeries()->setCurrent(ctx.getCurrentBarIndex(), std::cos(dval));
            return ctx.getResultSeries();
        },
        .min_args = 1,
        .max_args = 1
    };
    built_in_funcs["exp"] = {
        .function = [](FunctionContext &ctx) -> Value {
            double dval = ctx.getArgAsNumeric(0);
            ctx.getResultSeries()->setCurrent(ctx.getCurrentBarIndex(), std::exp(dval));
            return ctx.getResultSeries();
        },
        .min_args = 1,
        .max_args = 1
    };
    built_in_funcs["floor"] = {
        .function = [](FunctionContext &ctx) -> Value {
            double dval = ctx.getArgAsNumeric(0);
            ctx.getResultSeries()->setCurrent(ctx.getCurrentBarIndex(), std::floor(dval));
            return ctx.getResultSeries();
        },
        .min_args = 1,
        .max_args = 1
    };
    built_in_funcs["facepart"] = {
        .function = [](FunctionContext &ctx) -> Value {
            double dval = ctx.getArgAsNumeric(0);
            ctx.getResultSeries()->setCurrent(ctx.getCurrentBarIndex(), std::trunc(dval));
            return ctx.getResultSeries();
        },
        .min_args = 1,
        .max_args = 1
    };
    built_in_funcs["intpart"] = {
        .function = [](FunctionContext &ctx) -> Value {
            double dval = ctx.getArgAsNumeric(0);
            double result = std::isnan(dval) ? NAN : static_cast<double>(static_cast<long long>(dval));
            ctx.getResultSeries()->setCurrent(ctx.getCurrentBarIndex(), result);
            return ctx.getResultSeries();
        },
        .min_args = 1,
        .max_args = 1
    };
    built_in_funcs["ln"] = {
        .function = [](FunctionContext &ctx) -> Value {
            double dval = ctx.getArgAsNumeric(0);
            ctx.getResultSeries()->setCurrent(ctx.getCurrentBarIndex(), std::log(dval));
            return ctx.getResultSeries();
        },
        .min_args = 1,
        .max_args = 1
    };
    built_in_funcs["log"] = {
        .function = [](FunctionContext &ctx) -> Value {
            double dval = ctx.getArgAsNumeric(0);
            ctx.getResultSeries()->setCurrent(ctx.getCurrentBarIndex(), std::log10(dval));
            return ctx.getResultSeries();
        },
        .min_args = 1,
        .max_args = 1
    };
    built_in_funcs["max"] = {
        .function = [](FunctionContext &ctx) -> Value {
            double dval1 = ctx.getArgAsNumeric(0);
            double dval2 = ctx.getArgAsNumeric(1);
            double result = (std::isnan(dval1) || std::isnan(dval2)) ? NAN : std::max(dval1, dval2);
            ctx.getResultSeries()->setCurrent(ctx.getCurrentBarIndex(), result);
            return ctx.getResultSeries();
        },
        .min_args = 2,
        .max_args = 2
    };
    built_in_funcs["min"] = {
        .function = [](FunctionContext &ctx) -> Value {
            double dval1 = ctx.getArgAsNumeric(0);
            double dval2 = ctx.getArgAsNumeric(1);
            double result = (std::isnan(dval1) || std::isnan(dval2)) ? NAN : std::min(dval1, dval2);
            ctx.getResultSeries()->setCurrent(ctx.getCurrentBarIndex(), result);
            return ctx.getResultSeries();
        },
        .min_args = 2,
        .max_args = 2
    };
    built_in_funcs["mod"] = {
        .function = [](FunctionContext &ctx) -> Value {
            double dividend = ctx.getArgAsNumeric(0);
            double divisor = ctx.getArgAsNumeric(1);
            double result = (std::isnan(dividend) || std::isnan(divisor) || divisor == 0.0)
                ? NAN : static_cast<double>(static_cast<long long>(dividend) % static_cast<long long>(divisor));
            ctx.getResultSeries()->setCurrent(ctx.getCurrentBarIndex(), result);
            return ctx.getResultSeries();
        },
        .min_args = 2,
        .max_args = 2
    };
    built_in_funcs["pow"] = {
        .function = [](FunctionContext &ctx) -> Value {
            double base = ctx.getArgAsNumeric(0);
            double exponent = ctx.getArgAsNumeric(1);
            ctx.getResultSeries()->setCurrent(ctx.getCurrentBarIndex(), std::pow(base, exponent));
            return ctx.getResultSeries();
        },
        .min_args = 2,
        .max_args = 2
    };
    built_in_funcs["rand"] = {
        .function = [](FunctionContext &ctx) -> Value {
            double random_value = static_cast<double>(rand()) / RAND_MAX;
            ctx.getResultSeries()->setCurrent(ctx.getCurrentBarIndex(), random_value);
            return ctx.getResultSeries();
        },
        .min_args = 0,
        .max_args = 0
    };
    built_in_funcs["round"] = {
        .function = [](FunctionContext &ctx) -> Value {
            double dval = ctx.getArgAsNumeric(0);
            double result = NAN;
            if (!std::isnan(dval)) {
                if (ctx.argCount() == 2) {
                    int decimal_places = static_cast<int>(ctx.getArgAsNumeric(1));
                    double factor = std::pow(10.0, decimal_places);
                    result = std::round(dval * factor) / factor;
                } else {
                    result = std::round(dval);
                }
            }
            ctx.getResultSeries()->setCurrent(ctx.getCurrentBarIndex(), result);
            return ctx.getResultSeries();
        },
        .min_args = 1,
        .max_args = 2
    };
    built_in_funcs["round2"] = {
        .function = [](FunctionContext &ctx) -> Value {
            double dval = ctx.getArgAsNumeric(0);
            int decimal_places = static_cast<int>(ctx.getArgAsNumeric(1));
            double result = NAN;
            if (!std::isnan(dval)) {
                double factor = std::pow(10.0, decimal_places);
                result = std::round(dval * factor) / factor;
            }
            ctx.getResultSeries()->setCurrent(ctx.getCurrentBarIndex(), result);
            return ctx.getResultSeries();
        },
        .min_args = 2,
        .max_args = 2
    };
    built_in_funcs["sign"] = {
        .function = [](FunctionContext &ctx) -> Value {
            double dval = ctx.getArgAsNumeric(0);
            double result = std::isnan(dval) ? NAN : ((dval > 0) ? 1.0 : ((dval < 0) ? -1.0 : 0.0));
            ctx.getResultSeries()->setCurrent(ctx.getCurrentBarIndex(), result);
            return ctx.getResultSeries();
        },
        .min_args = 1,
        .max_args = 1
    };
    built_in_funcs["sin"] = {
        .function = [](FunctionContext &ctx) -> Value {
            double dval = ctx.getArgAsNumeric(0);
            ctx.getResultSeries()->setCurrent(ctx.getCurrentBarIndex(), std::sin(dval));
            return ctx.getResultSeries();
        },
        .min_args = 1,
        .max_args = 1
    };
    built_in_funcs["sqrt"] = {
        .function = [](FunctionContext &ctx) -> Value {
            double dval = ctx.getArgAsNumeric(0);
            ctx.getResultSeries()->setCurrent(ctx.getCurrentBarIndex(), std::sqrt(dval));
            return ctx.getResultSeries();
        },
        .min_args = 1,
        .max_args = 1
    };
    built_in_funcs["tan"] = {
        .function = [](FunctionContext &ctx) -> Value {
            double dval = ctx.getArgAsNumeric(0);
            ctx.getResultSeries()->setCurrent(ctx.getCurrentBarIndex(), std::tan(dval));
            return ctx.getResultSeries();
        },
        .min_args = 1,
        .max_args = 1
    };
    //时间函数

    //绘图函数

    //选择函数
    built_in_funcs["if"] = {
        .function = [](FunctionContext &ctx) -> Value {
            bool condition = ctx.getVM().getBoolValue(ctx.getArg(0));
            double true_val = ctx.getArgAsNumeric(1);
            double false_val = ctx.getArgAsNumeric(2);
            
            double result = condition ? true_val : false_val;
            ctx.getResultSeries()->setCurrent(ctx.getCurrentBarIndex(), result);
            return ctx.getResultSeries();
        },
        .min_args = 3,
        .max_args = 3
    };
    built_in_funcs["ifc"] = { .function = [](FunctionContext &ctx) { return ctx.getResultSeries(); }, .min_args = 3, .max_args = 3 }; // placeholder
    built_in_funcs["iff"] = { .function = [](FunctionContext &ctx) { return ctx.getResultSeries(); }, .min_args = 3, .max_args = 3 }; // placeholder
    built_in_funcs["ifn"] = { .function = [](FunctionContext &ctx) { return ctx.getResultSeries(); }, .min_args = 3, .max_args = 3 }; // placeholder
    built_in_funcs["testskip"] = { .function = [](FunctionContext &ctx) { return ctx.getResultSeries(); }, .min_args = 1, .max_args = 1 }; // placeholder
    built_in_funcs["valuewhen"] = {
        .function = [](FunctionContext &ctx) -> Value {
            bool condition = ctx.getVM().getBoolValue(ctx.getArg(0));
            double source_val = ctx.getVM().getNumericValue(ctx.getArg(1));
            
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            
            double result_val;
            if (condition) {
                result_val = source_val;
            } else {
                result_val = (current_bar > 0) ? result_series->getCurrent(current_bar - 1) : NAN;
            }
            result_series->setCurrent(current_bar, result_val);
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };

    //统计函数
    built_in_funcs["avedev"] = {
        .function = [](FunctionContext &ctx) -> Value {
            auto source_series = ctx.getArgAsSeries(0);
            int length = static_cast<int>(ctx.getArgAsNumeric(1));
            
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();

            std::vector<double> values;
            for(int i = 0; i < length && current_bar - i >= 0; ++i) {
                double val = source_series->getCurrent(current_bar - i);
                if (!std::isnan(val)) values.push_back(val);
            }

            double avedev_val = NAN;
            if (values.size() > 0) {
                double sum = std::accumulate(values.begin(), values.end(), 0.0);
                double mean = sum / values.size();
                double sum_dev = 0.0;
                for (double val : values) {
                    sum_dev += std::abs(val - mean);
                }
                avedev_val = sum_dev / values.size();
            }
            result_series->setCurrent(current_bar, avedev_val);
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };
    built_in_funcs["beta"] = { .function = [](FunctionContext &ctx) { return ctx.getResultSeries(); }, .min_args = 2, .max_args = 2 }; // placeholder
    built_in_funcs["betax"] = { .function = [](FunctionContext &ctx) { return ctx.getResultSeries(); }, .min_args = 3, .max_args = 3 }; // placeholder

    built_in_funcs["covar"] = {
        .function = [](FunctionContext &ctx) -> Value {
            auto source1_series = ctx.getArgAsSeries(0);
            auto source2_series = ctx.getArgAsSeries(1);
            int length = static_cast<int>(ctx.getArgAsNumeric(2));
            
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            
            double sum_x = 0.0, sum_y = 0.0, sum_xy = 0.0;
            int count = 0;
            for (int i = 0; i < length && current_bar - i >= 0; ++i) {
                double x = source1_series->getCurrent(current_bar - i);
                double y = source2_series->getCurrent(current_bar - i);
                if (!std::isnan(x) && !std::isnan(y)) {
                    sum_x += x;
                    sum_y += y;
                    sum_xy += (x * y);
                    count++;
                }
            }

            double covar_val = NAN;
            if (count == length && count > 1) {
                double mean_x = sum_x / count;
                double mean_y = sum_y / count;
                covar_val = (sum_xy - count * mean_x * mean_y) / (count - 1);
            }
            result_series->setCurrent(current_bar, covar_val);
            return result_series;
        },
        .min_args = 3,
        .max_args = 3
    };

    built_in_funcs["devsq"] = {
        .function = [](FunctionContext &ctx) -> Value {
            auto source_series = ctx.getArgAsSeries(0);
            int length = static_cast<int>(ctx.getArgAsNumeric(1));
            
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            
            std::vector<double> values;
            for(int i = 0; i < length && current_bar - i >= 0; ++i) {
                double val = source_series->getCurrent(current_bar - i);
                if (!std::isnan(val)) values.push_back(val);
            }
            
            double devsq_val = NAN;
            if (values.size() > 0) {
                double sum = std::accumulate(values.begin(), values.end(), 0.0);
                double mean = sum / values.size();
                double sum_sq_dev = 0.0;
                for (double val : values) {
                    sum_sq_dev += std::pow(val - mean, 2);
                }
                devsq_val = sum_sq_dev;
            }
            result_series->setCurrent(current_bar, devsq_val);
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };

    built_in_funcs["forcast"] = { .function = [](FunctionContext &ctx) { return ctx.getResultSeries(); }, .min_args = 2, .max_args = 2 }; // placeholder
    built_in_funcs["relate"] = { .function = [](FunctionContext &ctx) { return ctx.getResultSeries(); }, .min_args = 3, .max_args = 3 }; // placeholder
    
    built_in_funcs["slope"] = {
        .function = [](FunctionContext &ctx) -> Value {
            auto source_series = ctx.getArgAsSeries(0);
            int length = static_cast<int>(ctx.getArgAsNumeric(1));
            
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            
            if (current_bar < length - 1) {
                result_series->setCurrent(current_bar, NAN);
            } else {
                double sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;
                int n = 0;
                for (int i = 0; i < length; ++i) {
                    double y = source_series->getCurrent(current_bar - i);
                    if (!std::isnan(y)) {
                        double x = length - 1 - i; // x = 0, 1, ..., length-1
                        sum_x += x;
                        sum_y += y;
                        sum_xy += x * y;
                        sum_x2 += x * x;
                        n++;
                    }
                }
                if (n == length && n > 1) {
                    double denominator = n * sum_x2 - sum_x * sum_x;
                    double slope = (denominator == 0) ? NAN : (n * sum_xy - sum_x * sum_y) / denominator;
                    result_series->setCurrent(current_bar, slope);
                } else {
                    result_series->setCurrent(current_bar, NAN);
                }
            }
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };

    built_in_funcs["stddev"] = built_in_funcs["std"] = {
        .function = [](FunctionContext &ctx) -> Value {
            auto source_series = ctx.getArgAsSeries(0);
            int length = static_cast<int>(ctx.getArgAsNumeric(1));

            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            
            std::vector<double> values;
            for(int i = 0; i < length && current_bar - i >= 0; ++i) {
                double val = source_series->getCurrent(current_bar - i);
                if (!std::isnan(val)) values.push_back(val);
            }

            double std_dev = NAN;
            if (values.size() > 1 && values.size() == length) {
                double sum = std::accumulate(values.begin(), values.end(), 0.0);
                double mean = sum / values.size();
                double sq_sum = 0.0;
                for(const auto& d : values) sq_sum += (d - mean) * (d - mean);
                std_dev = std::sqrt(sq_sum / (values.size() - 1)); // Sample standard deviation
            }
            result_series->setCurrent(current_bar, std_dev);
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };

    built_in_funcs["stdp"] = {
        .function = [](FunctionContext &ctx) -> Value {
            auto source_series = ctx.getArgAsSeries(0);
            int length = static_cast<int>(ctx.getArgAsNumeric(1));

            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            
            std::vector<double> values;
            for(int i = 0; i < length && current_bar - i >= 0; ++i) {
                double val = source_series->getCurrent(current_bar - i);
                if (!std::isnan(val)) values.push_back(val);
            }

            double std_dev_p = NAN;
            if (values.size() > 0 && values.size() == length) {
                double sum = std::accumulate(values.begin(), values.end(), 0.0);
                double mean = sum / values.size();
                double sq_sum = 0.0;
                for(const auto& d : values) sq_sum += (d - mean) * (d - mean);
                std_dev_p = std::sqrt(sq_sum / values.size()); // Population standard deviation
            }
            result_series->setCurrent(current_bar, std_dev_p);
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };
    
    built_in_funcs["var"] = {
        .function = [](FunctionContext &ctx) -> Value {
            auto source_series = ctx.getArgAsSeries(0);
            int length = static_cast<int>(ctx.getArgAsNumeric(1));

            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            
            std::vector<double> values;
            for(int i = 0; i < length && current_bar - i >= 0; ++i) {
                double val = source_series->getCurrent(current_bar - i);
                if (!std::isnan(val)) values.push_back(val);
            }

            double variance = NAN;
            if (values.size() > 1 && values.size() == length) {
                double sum = std::accumulate(values.begin(), values.end(), 0.0);
                double mean = sum / values.size();
                double sq_sum = 0.0;
                for(const auto& d : values) sq_sum += (d - mean) * (d - mean);
                variance = sq_sum / (values.size() - 1); // Sample variance
            }
            result_series->setCurrent(current_bar, variance);
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };
    
    built_in_funcs["varp"] = {
        .function = [](FunctionContext &ctx) -> Value {
            auto source_series = ctx.getArgAsSeries(0);
            int length = static_cast<int>(ctx.getArgAsNumeric(1));

            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            
            std::vector<double> values;
            for(int i = 0; i < length && current_bar - i >= 0; ++i) {
                double val = source_series->getCurrent(current_bar - i);
                if (!std::isnan(val)) values.push_back(val);
            }

            double variance_p = NAN;
            if (values.size() > 0 && values.size() == length) {
                double sum = std::accumulate(values.begin(), values.end(), 0.0);
                double mean = sum / values.size();
                double sq_sum = 0.0;
                for(const auto& d : values) sq_sum += (d - mean) * (d - mean);
                variance_p = sq_sum / values.size(); // Population variance
            }
            result_series->setCurrent(current_bar, variance_p);
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };
    //逻辑函数
    built_in_funcs["cross"] = {
        .function = [](FunctionContext &ctx) -> Value {
            Value val1 = ctx.getArg(0);
            Value val2 = ctx.getArg(1);
            
            auto& vm = ctx.getVM();
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();

            double dval1 = vm.getNumericValue(val1);
            double dval2 = vm.getNumericValue(val2);

            auto *p1 = std::get_if<std::shared_ptr<Series>>(&val1);
            double prev_dval1 = (p1 && *p1) ? (*p1)->getCurrent(current_bar - 1) : dval1;
            auto *p2 = std::get_if<std::shared_ptr<Series>>(&val2);
            double prev_dval2 = (p2 && *p2) ? (*p2)->getCurrent(current_bar - 1) : dval2;
            
            bool result = false;
            if (!std::isnan(dval1) && !std::isnan(dval2) && !std::isnan(prev_dval1) && !std::isnan(prev_dval2)) {
                bool cross_up = (dval1 > dval2) && (prev_dval1 <= prev_dval2);
                bool cross_down = (dval1 < dval2) && (prev_dval1 >= prev_dval2);
                result = cross_up || cross_down;
            }
            result_series->setCurrent(current_bar, static_cast<double>(result));
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };

    built_in_funcs["downnday"] = { .function = [](FunctionContext &ctx) { return ctx.getResultSeries(); }, .min_args = 2, .max_args = 2 }; // placeholder
    
    built_in_funcs["every"] = {
        .function = [](FunctionContext &ctx) -> Value {
            auto condition_series = ctx.getArgAsSeries(0);
            int length = static_cast<int>(ctx.getArgAsNumeric(1));
            
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();
            
            bool result = false;
            if (current_bar >= length - 1) {
                bool all_true = true;
                for (int i = 0; i < length; ++i) {
                    double val = condition_series->getCurrent(current_bar - i);
                    if (std::isnan(val) || val == 0.0) {
                        all_true = false;
                        break;
                    }
                }
                result = all_true;
            }
            result_series->setCurrent(current_bar, static_cast<double>(result));
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };

    built_in_funcs["exist"] = {
        .function = [](FunctionContext &ctx) -> Value {
            auto condition_series = ctx.getArgAsSeries(0);
            int length = static_cast<int>(ctx.getArgAsNumeric(1));
            
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();

            bool result = false;
            for (int i = 0; i < length && current_bar - i >= 0; ++i) {
                double val = condition_series->getCurrent(current_bar - i);
                if (!std::isnan(val) && val != 0.0) {
                    result = true;
                    break;
                }
            }
            result_series->setCurrent(current_bar, static_cast<double>(result));
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };

    built_in_funcs["last"] = {
        .function = [](FunctionContext &ctx) -> Value {
            auto condition_series = ctx.getArgAsSeries(0);
            int start_offset = static_cast<int>(ctx.getArgAsNumeric(1));
            int end_offset = static_cast<int>(ctx.getArgAsNumeric(2));
            
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();

            if(start_offset == 0) start_offset = current_bar;
            if(end_offset == 0) end_offset = 1;

            bool all_true_in_range = true;
            for (int i = end_offset; i >= start_offset; --i) {
                int bar_to_check = current_bar - i;
                if (bar_to_check < 0) { all_true_in_range = false; break; }
                double cond_val = condition_series->getCurrent(bar_to_check);
                if (std::isnan(cond_val) || cond_val == 0.0) { all_true_in_range = false; break; }
            }
            result_series->setCurrent(current_bar, static_cast<double>(all_true_in_range));
            return result_series;
        },
        .min_args = 3,
        .max_args = 3
    };

    built_in_funcs["longcross"] = {
        .function = [](FunctionContext &ctx) -> Value {
            Value val1 = ctx.getArg(0);
            Value val2 = ctx.getArg(1);
            
            auto& vm = ctx.getVM();
            auto result_series = ctx.getResultSeries();
            int current_bar = ctx.getCurrentBarIndex();

            double dval1 = vm.getNumericValue(val1);
            double dval2 = vm.getNumericValue(val2);

            auto *p1 = std::get_if<std::shared_ptr<Series>>(&val1);
            double prev_dval1 = (p1 && *p1) ? (*p1)->getCurrent(current_bar - 1) : dval1;
            auto *p2 = std::get_if<std::shared_ptr<Series>>(&val2);
            double prev_dval2 = (p2 && *p2) ? (*p2)->getCurrent(current_bar - 1) : dval2;
            
            bool long_cross = false;
            if (!std::isnan(dval1) && !std::isnan(dval2) && !std::isnan(prev_dval1) && !std::isnan(prev_dval2)) {
                long_cross = (dval1 > dval2) && (prev_dval1 <= prev_dval2);
            }
            result_series->setCurrent(current_bar, static_cast<double>(long_cross));
            return result_series;
        },
        .min_args = 2,
        .max_args = 2
    };

    built_in_funcs["nday"] = { .function = [](FunctionContext &ctx) { return ctx.getResultSeries(); }, .min_args = 2, .max_args = 2 }; // placeholder
    
    built_in_funcs["not"] = {
        .function = [](FunctionContext &ctx) -> Value {
            double dval = ctx.getArgAsNumeric(0);
            double result = std::isnan(dval) ? NAN : static_cast<double>(dval == 0.0);
            ctx.getResultSeries()->setCurrent(ctx.getCurrentBarIndex(), result);
            return ctx.getResultSeries();
        },
        .min_args = 1,
        .max_args = 1
    };
    
    built_in_funcs["upnday"] = { .function = [](FunctionContext &ctx) { return ctx.getResultSeries(); }, .min_args = 2, .max_args = 2 }; // placeholder

    built_in_funcs["isnull"] = {
        .function = [](FunctionContext &ctx) -> Value {
            double dval = ctx.getArgAsNumeric(0);
            ctx.getResultSeries()->setCurrent(ctx.getCurrentBarIndex(), static_cast<double>(std::isnan(dval)));
            return ctx.getResultSeries();
        },
        .min_args = 1,
        .max_args = 1
    };

}