#pragma once

#include "DataSource.h"
#include "duckdb.h"
#include <string>
#include <memory>

// 从JSON文件读取数据的数据源
class JsonDataSource : public DataSource {
public:
    explicit JsonDataSource(const std::string& file_path);
    ~JsonDataSource() override;
    void loadData(PineVM& vm) override;
    int getNumBars() const override;

private:
    std::string file_path;
    int num_bars;
    duckdb_database db = nullptr;
    duckdb_connection con = nullptr;
    void initialize();
};