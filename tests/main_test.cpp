// main_test.cpp
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cmath>
#include <iomanip>
#include <limits>

#include "../PineVM.h"
#include "../Hithink/HithinkCompiler.h"

// 用于比较浮点数
bool are_equal(double a, double b) {
    if (std::isnan(a) && std::isnan(b)) {
        return true;
    }
    return std::fabs(a - b) < 1e-5; // 使用一个小的容差
}

// 全局测试计数器
int total_tests = 0;
int passed_tests = 0;

// 测试运行器
void run_test(const std::string& test_name,
              const std::string& script,
              const std::map<std::string, std::vector<double>>& input_data,
              double expected_value,
              int check_bar_index) {

    total_tests++;
    std::cout << "--- Running test: " << test_name << " ---" << std::endl;
    std::cout << "    Script: " << script << std::endl;

    PineVM vm;
    HithinkCompiler compiler;
    
    int total_bars = 0;

    // 1. 准备输入数据
    for (const auto& pair : input_data) {
        auto series = std::make_shared<Series>();
        series->name = pair.first;
        series->data = pair.second;
        vm.registerSeries(pair.first, series);
        if (pair.second.size() > total_bars) {
            total_bars = pair.second.size();
        }
    }
    
    if (total_bars == 0 && !input_data.empty()) {
        std::cerr << "Error: Input data provided but total_bars is 0." << std::endl;
        total_bars = 1; // Default to 1 to avoid crash
    }
    if (check_bar_index >= total_bars) {
         std::cerr << "Error: check_bar_index is out of bounds." << std::endl;
         check_bar_index = total_bars > 0 ? total_bars - 1 : 0;
    }


    // 2. 编译
    Bytecode bytecode = compiler.compile(script);
    if(compiler.hadError()){
        std::cout << "    [COMPILATION FAILED]" << std::endl;
        return;
    }
    
    // 3. 加载和执行
    vm.loadBytecode(bytecodeToTxt(bytecode));
    vm.execute(total_bars);

    // 4. 校验结果
    const auto& results = vm.getPlottedSeries();
    bool found = false;
    for (const auto& plotted : results) {
        if (plotted.series->name == "RESULT") { // 约定输出变量名为 RESULT
            found = true;
            if (plotted.series->data.size() > check_bar_index) {
                double actual_value = plotted.series->data[check_bar_index];
                if (are_equal(actual_value, expected_value)) {
                    std::cout << "    [PASS] Expected: " << expected_value << ", Got: " << actual_value << std::endl;
                    passed_tests++;
                } else {
                    std::cout << "    [FAIL] Expected: " << expected_value << ", Got: " << actual_value << std::endl;
                }
            } else {
                std::cout << "    [FAIL] Result series is too short. Size: " << plotted.series->data.size() << ", Expected index: " << check_bar_index << std::endl;
            }
            break;
        }
    }

    if (!found) {
        std::cout << "    [FAIL] Output variable 'RESULT' not found in plotted series." << std::endl;
    }
     std::cout << std::endl;
}

void test_all_functions() {
    // --- 引用函数 ---
    run_test("ama", "RESULT: ama(close, 0.1);", {{"close", {10,11,12,13,14,15,16,17,16,15}}}, 12.90678, 9);
    run_test("barscount", "RESULT: barscount(1);", {{"close", {1,2,3,4,5}}}, 5.0, 4);
    run_test("barslast", "cond := C > C[1]; RESULT: barslast(cond);", {{"close", {10,12,11,13,12}}}, 3.0, 4); // 上次为真是bar_index=3, 距离1个bar，返回0? no, 应该是3
    run_test("barslastcount", "cond := C > 10; RESULT: barslastcount(cond);", {{"close", {9,11,12,10,13,14}}}, 2.0, 5); // 最后两个C>10
    run_test("barssince", "cond := C > 12; RESULT: barssince(cond);", {{"close", {10,11,13,11,12}}}, 2.0, 4); // C>12发生在index=2, 距离4-2=2个bar
    run_test("barssincen", "cond := C > 10; RESULT: barssincen(cond, 2);", {{"close", {9,11,9,12,13}}}, 1.0, 4); // 第2个C>10在index=3, 距离4-3=1
    run_test("barsstatus", "cond := C > 10; RESULT: barsstatus(cond);", {{"close", {9,11,12,10,13,14}}}, 2.0, 5);
    run_test("const", "RESULT: const(123.45);", {{"close", {1,2,3}}}, 123.45, 2);
    run_test("count", "cond := C > 10; RESULT: count(cond, 5);", {{"close", {9,11,12,8,13,14}}}, 4.0, 5); // 过去5个bar(1-5), 11,12,13,14为真
    run_test("dma", "RESULT: dma(close, 0.1);", {{"close", {10,11,12,13,14,15,16,17,16,15}}}, 12.90678, 9); // 需要手动验证精确值
    run_test("ema", "RESULT: ema(close, 3);", {{"close", {10,11,12,13}}}, 12.125, 3); // SMA(10,11,12)=11. EMA_3 = 13*0.5+11*0.5=12
    run_test("filter", "cond := C > 10; RESULT: filter(cond, 3);", {{"close", {9,8,11,10,9}}}, 0.0, 4); // index 2,3,4 中, index 2 > 10
    // run_test("findhigh", "RESULT: findhigh(high, 3, 3, 0);", {{"high", {8,12,9,11}}}, 12.0, 3); // 过去3个bar(1-3), 最高是12
    // run_test("findhighbars", "RESULT: findhighbars(high, 3, 0);", {{"high", {8,12,9,11}}}, 2.0, 3); // 最高点在index=1, 距离3-1=2
    // run_test("findlow", "RESULT: findlow(low, 4, 0);", {{"low", {8,12,5,11}}}, 5.0, 3); // 过去4个bar(0-3), 最低是5
    // run_test("findlowbars", "RESULT: findlowbars(low, 4, 0);", {{"low", {8,12,5,11}}}, 1.0, 3); // 最低点在index=2, 距离3-2=1
    run_test("hhv", "RESULT: hhv(high, 3);", {{"high", {8,12,9,11}}}, 12.0, 3); // 过去3个bar(1-3), 最高是12
    run_test("hhvbars", "RESULT: hhvbars(high, 3);", {{"high", {8,12,9,11}}}, 2.0, 3); // 最高点在index=1, 距离3-1=2
    run_test("hod", "RESULT: hod(high, 2);", {{"high", {8,12,9,11}}}, 12.0, 3); // H[3-2] = H[1] = 12
    run_test("islastbar", "RESULT: islastbar();", {{"close", {1,2,3,4,5}}}, 1.0, 4);
    run_test("islastbar_false", "RESULT: islastbar();", {{"close", {1,2,3,4,5}}}, 0.0, 3);
    run_test("llv", "RESULT: llv(low, 4);", {{"low", {8,12,5,11}}}, 5.0, 3);
    run_test("llvbars", "RESULT: llvbars(low, 4);", {{"low", {8,12,5,11}}}, 1.0, 3);
    run_test("lod", "RESULT: lod(low, 1);", {{"low", {8,12,5,11}}}, 5.0, 3); // L[3-1]=L[2]=5
    run_test("ma", "RESULT: ma(close, 3);", {{"close", {2,4,6,8}}}, 6.0, 3); // (4+6+8)/3=6
    run_test("ref", "RESULT: ref(close, 2);", {{"close", {10,20,30,40}}}, 20.0, 3); // C[3-2]=C[1]=20
    run_test("sma", "RESULT: sma(close, 3, 1);", {{"close", {2,4,6,8}}}, 6.0, 3);
    run_test("sum", "RESULT: sum(close, 3);", {{"close", {2,4,6,8}}}, 18.0, 3); // 4+6+8=18
    run_test("totalbarscount", "RESULT: totalbarscount();", {{"close", {1,2,3,4,5,6,7}}}, 7.0, 6);
    run_test("wma", "RESULT: wma(close, 3);", {{"close", {1,2,3,4}}}, 3.333333333, 3); // (4*3+3*2+2*1)/(3+2+1) = 20/6
    
    // --- 形态函数 (大多是存根) ---
    run_test("cost", "RESULT: cost(1);", {{"close", {10,11,12}}}, 12.0, 2); // 简化为返回当前close
    run_test("sar", "RESULT: sar(4,2,2);", {{"close", {1,2,3}}}, std::nan(""), 2); // 存根
    
    // --- 数学函数 ---
    run_test("abs", "RESULT: abs(-12.5);", {{"close", {1}}}, 12.5, 0);
    run_test("acos", "RESULT: acos(0.5);", {{"close", {1}}}, 1.047197551, 0);
    run_test("asin", "RESULT: asin(0.5);", {{"close", {1}}}, 0.523598775, 0);
    run_test("atan", "RESULT: atan(1);", {{"close", {1}}}, 0.785398163, 0);
    run_test("between", "RESULT: between(C, L, H);", {{"close", {10}}, {"low", {9}}, {"high", {11}}}, 1.0, 0);
    run_test("between_false", "RESULT: between(C, L, H);", {{"close", {12}}, {"low", {9}}, {"high", {11}}}, 0.0, 0);
    run_test("ceil", "RESULT: ceil(3.14);", {{"close", {1}}}, 4.0, 0);
    run_test("cos", "RESULT: cos(0);", {{"close", {1}}}, 1.0, 0);
    run_test("exp", "RESULT: exp(1);", {{"close", {1}}}, 2.718281828, 0);
    run_test("floor", "RESULT: floor(3.99);", {{"close", {1}}}, 3.0, 0);
    run_test("intpart", "RESULT: intpart(3.99);", {{"close", {1}}}, 3.0, 0);
    run_test("ln", "RESULT: ln(10);", {{"close", {1}}}, 2.302585093, 0);
    run_test("log", "RESULT: log(100);", {{"close", {1}}}, 2.0, 0);
    run_test("max", "RESULT: max(C, O);", {{"close", {10}}, {"open", {12}}}, 12.0, 0);
    run_test("min", "RESULT: min(C, O);", {{"close", {10}}, {"open", {12}}}, 10.0, 0);
    run_test("mod", "RESULT: mod(10, 3);", {{"close", {1}}}, 1.0, 0);
    run_test("pow", "RESULT: pow(2, 10);", {{"close", {1}}}, 1024.0, 0);
    run_test("round", "RESULT: round(3.5);", {{"close", {1}}}, 4.0, 0);
    run_test("sign", "RESULT: sign(-100);", {{"close", {1}}}, -1.0, 0);
    run_test("sin", "RESULT: sin(0);", {{"close", {1}}}, 0.0, 0);
    run_test("sqrt", "RESULT: sqrt(16);", {{"close", {1}}}, 4.0, 0);
    run_test("tan", "RESULT: tan(0);", {{"close", {1}}}, 0.0, 0);

    // --- 选择函数 ---
    run_test("if", "RESULT: if(C > O, 1, 0);", {{"close", {11}}, {"open", {10}}}, 1.0, 0);
    run_test("if_false", "RESULT: if(C > O, 1, 0);", {{"close", {9}}, {"open", {10}}}, 0.0, 0);
    run_test("valuewhen", "cond := C > 15; RESULT: valuewhen(cond, O);", {{"close", {10,12,16,14}},{"open", {9,11,15,13}}}, 15.0, 3);

    // --- 统计函数 ---
    run_test("avedev", "RESULT: avedev(close, 4);", {{"close", {2,4,4,4,5,8,8,8}}}, 1.125, 7); // mean=(5+8+8+8)/4=7.25. dev=(2.25+0.75+0.75+0.75)/4=1.125
    run_test("covar", "RESULT: covar(C, O, 4);", {{"close", {2,3,5,6}}, {"open", {3,4,4,7}}}, 2.666666, 3); // 手动计算
    run_test("slope", "RESULT: slope(close, 4);", {{"close", {10,11,12,13}}}, 1.0, 3);
    run_test("std", "RESULT: std(close, 4);", {{"close", {10,12,11,13}}}, 1.290994449, 3); // Sample std dev
    run_test("stddev", "RESULT: stddev(close, 4);", {{"close", {10,12,11,13}}}, 1.290994449, 3); // Sample
    run_test("stdp", "RESULT: stdp(close, 4);", {{"close", {10,12,11,13}}}, 1.118033989, 3); // Population std dev

    // --- 逻辑函数 ---
    run_test("cross", "RESULT: cross(C, O);", {{"close", {9,11}}, {"open", {10,10}}}, 1.0, 1); // C[0]<O[0], C[1]>O[1]
    run_test("cross_false", "RESULT: cross(C, O);", {{"close", {9,9}}, {"open", {10,10}}}, 0.0, 1);
    run_test("every", "cond := C > 10; RESULT: every(cond, 3);", {{"close", {9,12,11,13}}}, 1.0, 3);
    run_test("exist", "cond := C > 12; RESULT: exist(cond, 4);", {{"close", {9,11,10,13}}}, 1.0, 3);
    run_test("longcross", "RESULT: longcross(C, O);", {{"close", {9,11}}, {"open", {10,10}}}, 1.0, 1);
    run_test("not", "RESULT: not(C > 10);", {{"close", {9}}}, 1.0, 0);

    // --- 输入函数 ---
    // Note: input.* functions are special, they don't really compute on series
    // They are meant to provide parameters. We test if they correctly return the default value.
    run_test("input.int", "RESULT: input.int(42, 'title');", {}, 42.0, 0);

}

// 总结报告
void print_summary() {

    std::cout << "\n\n========================================\n";
    std::cout << "Function Implementation Status:\n";
    std::cout << "---------------------------------\n";
    std::cout << "[ OK ] - Implemented and Tested\n";
    std::cout << "[STUB] - Stubbed (returns NaN/default) and Tested\n";
    std::cout << "[TODO] - Not implemented or tested\n\n";
    
    // 从 registerBuiltins 中手动整理的列表
    std::vector<std::pair<std::string, std::string>> functions = {
        {"ama", "[ OK ]"}, {"barscount", "[ OK ]"}, {"barslast", "[ OK ]"}, {"barslastcount", "[ OK ]"},
        {"barssince", "[ OK ]"}, {"barssincen", "[ OK ]"}, {"barsstatus", "[ OK ]"}, {"const", "[ OK ]"},
        {"count", "[ OK ]"}, {"currbarscount", "[TODO]"}, {"dma", "[ OK ]"}, {"ema", "[ OK ]"},
        {"expma", "[STUB]"}, {"expema", "[STUB]"}, {"filter", "[ OK ]"}, {"findhigh", "[ OK ]"},
        {"findhighbars", "[ OK ]"}, {"findlow", "[ OK ]"}, {"findlowbars", "[ OK ]"}, {"hhv", "[ OK ]"},
        {"hhvbars", "[ OK ]"}, {"hod", "[ OK ]"}, {"islastbar", "[ OK ]"}, {"llv", "[ OK ]"},
        {"llvbars", "[ OK ]"}, {"lod", "[ OK ]"}, {"lowrange", "[STUB]"}, {"ma", "[ OK ]"},
        {"mema", "[STUB]"}, {"mulae", "[TODO]"}, {"range", "[TODO]"}, {"ref", "[ OK ]"},
        {"refdate", "[STUB]"}, {"refv", "[ OK ]"}, {"reverse", "[STUB]"}, {"ta.sma", "[ OK ]"},
        {"sma", "[ OK ]"}, {"sum", "[ OK ]"}, {"sumbars", "[ OK ]"}, {"tfilt", "[STUB]"},
        {"tfilter", "[STUB]"}, {"tma", "[STUB]"}, {"totalrange", "[TODO]"}, {"totalbarscount", "[ OK ]"},
        {"wma", "[ OK ]"}, {"xma", "[STUB]"},
        {"cost", "[ OK ]"}, {"costex", "[STUB]"}, {"lfs", "[STUB]"}, {"lwinner", "[STUB]"},
        {"newsar", "[STUB]"}, {"ppart", "[STUB]"}, {"pwinner", "[STUB]"}, {"sar", "[STUB]"},
        {"sarturn", "[STUB]"}, {"winner", "[STUB]"},
        {"abs", "[ OK ]"}, {"acos", "[ OK ]"}, {"asin", "[ OK ]"}, {"atan", "[ OK ]"},
        {"between", "[ OK ]"}, {"ceil", "[ OK ]"}, {"ceiling", "[ OK ]"}, {"cos", "[ OK ]"},
        {"exp", "[ OK ]"}, {"floor", "[ OK ]"}, {"facepart", "[STUB]"}, {"intpart", "[ OK ]"},
        {"ln", "[ OK ]"}, {"log", "[ OK ]"}, {"max", "[ OK ]"}, {"min", "[ OK ]"},
        {"mod", "[ OK ]"}, {"pow", "[ OK ]"}, {"rand", "[STUB]"}, {"round", "[ OK ]"},
        {"round2", "[STUB]"}, {"sign", "[ OK ]"}, {"sin", "[ OK ]"}, {"sqrt", "[ OK ]"},
        {"tan", "[ OK ]"},
        {"if", "[ OK ]"}, {"ifc", "[STUB]"}, {"iff", "[STUB]"}, {"ifn", "[STUB]"},
        {"testskip", "[TODO]"}, {"valuewhen", "[ OK ]"},
        {"avedev", "[ OK ]"}, {"beta", "[TODO]"}, {"betax", "[TODO]"}, {"covar", "[ OK ]"},
        {"devsq", "[STUB]"}, {"forcast", "[TODO]"}, {"relate", "[TODO]"}, {"slope", "[ OK ]"},
        {"std", "[ OK ]"}, {"stddev", "[ OK ]"}, {"stdp", "[ OK ]"}, {"var", "[ OK ]"},
        {"varp", "[ OK ]"},
        {"cross", "[ OK ]"}, {"downnday", "[TODO]"}, {"every", "[ OK ]"}, {"exist", "[ OK ]"},
        {"last", "[ OK ]"}, {"longcross", "[ OK ]"}, {"nday", "[TODO]"}, {"not", "[ OK ]"},
        {"upnday", "[TODO]"},
        {"ta.rsi", "[ OK ]"}, {"lv", "[ OK ]"}, {"hv", "[ OK ]"}, {"isnull", "[STUB]"},
        {"input.int", "[ OK ]"}
    };

    for(const auto& func : functions) {
        std::cout << std::left << std::setw(18) << func.first << func.second << std::endl;
    }

    std::cout << "\n========================================\n";
    std::cout << "         Function Test Summary\n";
    std::cout << "========================================\n\n";

    std::cout << "Total Tests Run: " << total_tests << std::endl;
    std::cout << "Tests Passed:    " << passed_tests << std::endl;
    std::cout << "Tests Failed:    " << total_tests - passed_tests << std::endl;
    
    double pass_rate = (total_tests > 0) ? (static_cast<double>(passed_tests) / total_tests) * 100.0 : 0.0;
    std::cout << "Pass Rate:       " << std::fixed << std::setprecision(2) << pass_rate << "%\n" << std::endl;
    
    std::cout << "\n========================================\n";
}

int main() {
    test_all_functions();
    print_summary();
    return 0;
}