#pragma once

#include "PineVM.h"
#include <string>
#include <vector>
#include <map>

// 数据源的抽象基类
class DataSource {
public:
    virtual ~DataSource() = default;
    // 加载数据到VM中
    virtual void loadData(PineVM& vm) = 0;
    // 获取K线总数
    virtual int getNumBars() const = 0;
};

// 一个生成示例数据的模拟数据源
class MockDataSource : public DataSource {
public:
    explicit MockDataSource(int num_bars);
    void loadData(PineVM& vm) override;
    int getNumBars() const override;

private:
    int num_bars;
    std::map<std::string, std::vector<double>> market_data;
    void generateData();
};