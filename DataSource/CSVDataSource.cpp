#include "CSVDataSource.h"
#include "../PineVM.h" // For PineVM definition
#include <iostream>
#include <stdexcept>

CSVDataSource::CSVDataSource(const std::string& file_path) : file_path(file_path), num_bars(0) {
    initialize();
}
 
CSVDataSource::~CSVDataSource() {
    if (con) {
        duckdb_disconnect(&con);
    }
    if (db) {
        duckdb_close(&db);
    }
}

void CSVDataSource::initialize() {
    if (duckdb_open(nullptr, &db) != DuckDBSuccess) {
        throw std::runtime_error("Failed to open in-memory DuckDB database.");
    }
    if (duckdb_connect(db, &con) != DuckDBSuccess) {
        duckdb_close(&db); // cleanup
        throw std::runtime_error("Failed to connect to DuckDB database.");
    }

    // 1. Get row count
    std::string count_query = "SELECT count(*) FROM read_csv_auto('" + file_path + "')";
    duckdb_result count_result;
    if (duckdb_query(con, count_query.c_str(), &count_result) != DuckDBSuccess) {
        std::string error_msg = "DuckDB query failed: " + std::string(duckdb_result_error(&count_result));
        duckdb_destroy_result(&count_result);
        throw std::runtime_error(error_msg);
    }
    if (duckdb_row_count(&count_result) == 0) {
        duckdb_destroy_result(&count_result);
        throw std::runtime_error("Failed to get row count from CSV file '" + file_path + "'.");
    }
    num_bars = duckdb_value_int64(&count_result, 0, 0);
    duckdb_destroy_result(&count_result);

    // 2. Create a table from the CSV for later use
    std::string create_table_query = "CREATE TABLE market_data AS SELECT * FROM read_csv_auto('" + file_path + "')";
    duckdb_result create_result;
    if (duckdb_query(con, create_table_query.c_str(), &create_result) != DuckDBSuccess) {
        std::string error_msg = "Failed to create market_data table: " + std::string(duckdb_result_error(&create_result));
        duckdb_destroy_result(&create_result);
        throw std::runtime_error(error_msg);
    }
    duckdb_destroy_result(&create_result);
}

void CSVDataSource::loadData(PineVM& vm) {
    duckdb_result result;
    std::string query = "SELECT open, high, low, close, volume FROM market_data";
    if (duckdb_query(con, query.c_str(), &result) != DuckDBSuccess) {
        std::string error_msg = "Failed to query market_data table: " + std::string(duckdb_result_error(&result));
        duckdb_destroy_result(&result);
        throw std::runtime_error(error_msg);
    }

    // Get pointers to the series in the VM
    auto* open_series = vm.getSeries("open");
    auto* high_series = vm.getSeries("high");
    auto* low_series = vm.getSeries("low");
    auto* close_series = vm.getSeries("close");
    auto* volume_series = vm.getSeries("volume");

    // Check if all series were found
    if (!open_series || !high_series || !low_series || !close_series || !volume_series) {
        duckdb_destroy_result(&result);
        throw std::runtime_error("One or more required series (open, high, low, close, volume) not found in PineVM.");
    }

    idx_t row_count = duckdb_row_count(&result);
    for (idx_t r = 0; r < row_count; ++r) {
        open_series->data.push_back(duckdb_value_double(&result, 0, r));
        high_series->data.push_back(duckdb_value_double(&result, 1, r));
        low_series->data.push_back(duckdb_value_double(&result, 2, r));
        close_series->data.push_back(duckdb_value_double(&result, 3, r));
        volume_series->data.push_back(duckdb_value_double(&result, 4, r));
    }

    duckdb_destroy_result(&result);
}

int CSVDataSource::getNumBars() const {
    return num_bars;
}