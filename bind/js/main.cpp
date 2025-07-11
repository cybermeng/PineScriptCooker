#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <emscripten/bind.h>

#include "../../PineVM.h"

// 从单行"JSON"中提取值的辅助函数 (非鲁棒)
double extract_json_double(const std::string& line, const std::string& key) {
    size_t key_pos = line.find(key);
    if (key_pos == std::string::npos) return NAN;
    size_t colon_pos = line.find(':', key_pos);
    if (colon_pos == std::string::npos) return NAN;
    size_t value_start = colon_pos + 1;
    size_t value_end = line.find_first_of(",}", value_start);
    if (value_end == std::string::npos) return NAN;

    std::string val_str = line.substr(value_start, value_end - value_start);
    try {
        return std::stod(val_str);
    } catch (...) {
        return NAN;
    }
}

// 解析金融数据字符串并将其加载到VM中
void parse_and_load_data(PineVM& vm, const std::string& data_string) {
    // 注册VM中需要的序列
    vm.registerSeries("time", std::make_shared<Series>());
    vm.registerSeries("open", std::make_shared<Series>());
    vm.registerSeries("high", std::make_shared<Series>());
    vm.registerSeries("low", std::make_shared<Series>());
    vm.registerSeries("close", std::make_shared<Series>());
    vm.registerSeries("volume", std::make_shared<Series>());

    // 获取指向序列的指针以便快速访问
    auto* time_series = vm.getSeries("time");
    auto* open_series = vm.getSeries("open");
    auto* high_series = vm.getSeries("high");
    auto* low_series = vm.getSeries("low");
    auto* close_series = vm.getSeries("close");
    auto* volume_series = vm.getSeries("volume");

    if (!time_series || !open_series || !high_series || !low_series || !close_series || !volume_series) {
        throw std::runtime_error("Failed to register one or more series in PineVM.");
    }
    
    std::stringstream ss(data_string);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.empty() || line.find("{") == std::string::npos) continue;

        // "7": open, "8": high, "9": low, "11": close, "13": volume
        time_series->data.push_back(extract_json_double(line, "\"trade_date\":"));
        open_series->data.push_back(extract_json_double(line, "\"7\":"));
        high_series->data.push_back(extract_json_double(line, "\"8\":"));
        low_series->data.push_back(extract_json_double(line, "\"9\":"));
        close_series->data.push_back(extract_json_double(line, "\"11\":"));
        volume_series->data.push_back(extract_json_double(line, "\"13\":"));
    }
}

extern "C" {
    EMSCRIPTEN_KEEPALIVE
// 暴露给JavaScript的主函数
const char* run_pine_calculation(const char* bytecode_string_c, const char* financial_data_string_c) {
        std::string bytecode_string(bytecode_string_c);
        std::string financial_data_string(financial_data_string_c);

        // 将std::cout重定向到字符串流以捕获所有输出
        static std::string result_string; // 使用 static 变量来确保其生命周期足够长
        std::stringstream output_buffer;
        std::streambuf* old_cout_buf = std::cout.rdbuf(output_buffer.rdbuf());
        std::streambuf* old_cerr_buf = std::cerr.rdbuf(output_buffer.rdbuf());
        
        try {
            // ... (函数内部的 try-catch 块完全保持不变) ...
            int num_bars = 0;
            std::stringstream count_ss(financial_data_string);
            std::string line;
            while (std::getline(count_ss, line)) {
                if (!line.empty() && line.find("{") != std::string::npos) {
                    num_bars++;
                }
            }
            
            if (num_bars > 0) {
                std::cout << "\nVM start, bar number:" << num_bars << std::endl;
                PineVM vm(num_bars);
                parse_and_load_data(vm, financial_data_string);
                vm.loadBytecode(bytecode_string);
                vm.execute();
                vm.printPlottedResults();

                result_string = vm.getPlottedResultsAsString();
            }

        } catch (const std::exception& e) {
            std::cerr << "\n!!! C++ EXCEPTION CAUGHT !!!\n" << e.what() << std::endl;
        }
        
        // 恢复原始的 cout/cerr
        std::cout.rdbuf(old_cout_buf);
        std::cerr.rdbuf(old_cerr_buf);
        
        if(result_string.empty())
            result_string = output_buffer.str();
        return result_string.c_str(); // 返回一个 C 风格字符串指针
    }
}
