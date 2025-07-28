#include "DataSource.h"
#include <iostream>
#include <algorithm> // for std::max/min

MockDataSource::MockDataSource(int num_bars) : num_bars(num_bars) {
    generateData();
}

void MockDataSource::generateData() {
    std::vector<double> time_data(num_bars);
    std::vector<double> close_prices(num_bars);
    std::vector<double> open_prices(num_bars);
    std::vector<double> high_prices(num_bars);
    std::vector<double> low_prices(num_bars);
    std::vector<double> volume_data(num_bars);

    for (int i = 0; i < num_bars; ++i) {
        time_data[i] = 1672531200 + i * 60; // 从 2023-01-01 00:00:00 开始，每分钟一个 bar
        close_prices[i] = 100.0 + (i % 20 - 10) * 0.5;  // 示例震荡数据
        open_prices[i] = close_prices[i] - (i % 5 - 2) * 0.1;
        high_prices[i] = std::max(open_prices[i], close_prices[i]) + (i % 3) * 0.05;
        low_prices[i] = std::min(open_prices[i], close_prices[i]) - (i % 3) * 0.05;
        volume_data[i] = 1000.0 + (i % 5) * 100;
    }

    market_data["time"] = time_data;
    market_data["close"] = close_prices;
    market_data["open"] = open_prices;
    market_data["high"] = high_prices;
    market_data["low"] = low_prices;
    market_data["volume"] = volume_data;
}

void MockDataSource::loadData(PineVM& vm) {
    auto make_series = [](const std::string& name, const std::vector<double>& data) {
        auto series = std::make_shared<Series>();
        series->name = name;
        series->data = data;
        return series;
    };

    for (const auto& pair : market_data) {
        vm.registerSeries(pair.first, make_series(pair.first, pair.second));
    }
}

int MockDataSource::getNumBars() const {
    return num_bars;
}