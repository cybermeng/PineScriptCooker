#include "JsonDataSource.h"
#include "PineVM.h" // For PineVM definition
#include <iostream>
#include <stdexcept>

JsonDataSource::JsonDataSource(const std::string& file_path) : file_path(file_path), num_bars(0) {
    initialize();
}

JsonDataSource::~JsonDataSource() {
    if (con) {
        duckdb_disconnect(&con);
    }
    if (db) {
        duckdb_close(&db);
    }
}

void JsonDataSource::initialize() {
    if (duckdb_open(nullptr, &db) != DuckDBSuccess) {
        throw std::runtime_error("Failed to open in-memory DuckDB database.");
    }
    if (duckdb_connect(db, &con) != DuckDBSuccess) {
        duckdb_close(&db); // cleanup
        throw std::runtime_error("Failed to connect to DuckDB database.");
    }

    // 1. Get row count
    std::string count_query = "SELECT count(*) FROM read_json_auto('" + file_path + "')";
    duckdb_result count_result;
    if (duckdb_query(con, count_query.c_str(), &count_result) != DuckDBSuccess) {
        std::string error_msg = "DuckDB query failed: " + std::string(duckdb_result_error(&count_result));
        duckdb_destroy_result(&count_result);
        throw std::runtime_error(error_msg);
    }
    if (duckdb_row_count(&count_result) == 0) {
        duckdb_destroy_result(&count_result);
        throw std::runtime_error("Failed to get row count from JSON file '" + file_path + "'.");
    }
    num_bars = duckdb_value_int64(&count_result, 0, 0);
    duckdb_destroy_result(&count_result);

    // 2. Create a table from the JSON for later use
    std::string create_table_query = "CREATE TABLE market_data AS SELECT * FROM read_json_auto('" + file_path + "')";
    duckdb_result create_result;
    if (duckdb_query(con, create_table_query.c_str(), &create_result) != DuckDBSuccess) {
        std::string error_msg = "Failed to create market_data table from JSON: " + std::string(duckdb_result_error(&create_result));
        duckdb_destroy_result(&create_result);
        throw std::runtime_error(error_msg);
    }
    duckdb_destroy_result(&create_result);
    
}

void JsonDataSource::loadData(PineVM& vm) {
    duckdb_result result;
    // 修改查询以使用带引号的数字列名，匹配 amzn.json 和 aapl.json 等文件的格式。
    // DuckDB的read_json_auto会将这些键作为列名。
    // 我们使用 AS 来重命名它们，以便VM的其余部分可以按名称 ("open", "high", etc.) 找到它们。
    // 注意：这假设了JSON文件中的列名是 "7", "8", "9", "11", "13"。
    std::string query = R"(SELECT trade_date AS time, "7" AS open, "8" AS high, "9" AS low, "11" AS close, "13" AS volume FROM market_data ORDER BY "trade_date" ASC)";
    if (duckdb_query(con, query.c_str(), &result) != DuckDBSuccess) {
        std::string error_msg = "Failed to query market_data table: " + std::string(duckdb_result_error(&result));
        duckdb_destroy_result(&result);
        throw std::runtime_error(error_msg);
    }

    // 在加载数据之前，确保VM中存在这些序列
    vm.registerSeries("time", std::make_shared<Series>());
    vm.registerSeries("open", std::make_shared<Series>());
    vm.registerSeries("high", std::make_shared<Series>());
    vm.registerSeries("low", std::make_shared<Series>());
    vm.registerSeries("close", std::make_shared<Series>());
    vm.registerSeries("volume", std::make_shared<Series>());

    auto* time_series = vm.getSeries("time");
    auto* open_series = vm.getSeries("open");
    auto* high_series = vm.getSeries("high");
    auto* low_series = vm.getSeries("low");
    auto* close_series = vm.getSeries("close");
    auto* volume_series = vm.getSeries("volume");

    if (!time_series || !open_series || !high_series || !low_series || !close_series || !volume_series) {
        duckdb_destroy_result(&result);
        throw std::runtime_error("One or more required series (open, high, low, close, volume) not found in PineVM.");
    }

    for (idx_t r = 0; r < num_bars; ++r) {
        time_series->data.push_back(duckdb_value_double(&result, 0, r));
        open_series->data.push_back(duckdb_value_double(&result, 1, r));
        high_series->data.push_back(duckdb_value_double(&result, 2, r));
        low_series->data.push_back(duckdb_value_double(&result, 3, r));
        close_series->data.push_back(duckdb_value_double(&result, 4, r));
        volume_series->data.push_back(duckdb_value_double(&result, 5, r));
    }

    duckdb_destroy_result(&result);
}

int JsonDataSource::getNumBars() const {
    return num_bars;
}