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

    // 1. Get row count (remains the same)
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

    // 2. Create a table from the CSV.
    // *** KEY CHANGE HERE ***
    // We now tell DuckDB to interpret the 'time' column as a TIMESTAMP.
    // DuckDB's automatic parser is smart enough to handle both 'YYYY-MM-DD' and
    // 'YYYY-MM-DD HH:MM:SS' formats when the target type is TIMESTAMP.
    // We no longer need the 'dateformat' hint.
    std::string create_table_query = 
        "CREATE TABLE market_data AS SELECT * FROM read_csv_auto('" + file_path + "', "
        "columns={'time': 'TIMESTAMP', 'open': 'DOUBLE', 'high': 'DOUBLE', 'low': 'DOUBLE', 'close': 'DOUBLE'})";

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
    // This query remains IDENTICAL. The epoch() function works perfectly on both
    // DATE and TIMESTAMP types, converting them to a numeric Unix timestamp.
    std::string query = "SELECT epoch(time), strftime(time, '%Y%m%d'), open, high, low, close FROM market_data ORDER BY time ASC";

    if (duckdb_query(con, query.c_str(), &result) != DuckDBSuccess) {
        std::string error_msg = "Failed to query market_data table: " + std::string(duckdb_result_error(&result));
        duckdb_destroy_result(&result);
        throw std::runtime_error(error_msg);
    }
    
    vm.registerSeries("time", std::make_shared<Series>());
    vm.registerSeries("date", std::make_shared<Series>());
    vm.registerSeries("open", std::make_shared<Series>());
    vm.registerSeries("high", std::make_shared<Series>());
    vm.registerSeries("low", std::make_shared<Series>());
    vm.registerSeries("close", std::make_shared<Series>());

    auto* time_series = vm.getSeries("time");
    auto* date_series = vm.getSeries("date");
    auto* open_series = vm.getSeries("open");
    auto* high_series = vm.getSeries("high");
    auto* low_series = vm.getSeries("low");
    auto* close_series = vm.getSeries("close");

    if (!time_series || !date_series || !open_series || !high_series || !low_series || !close_series) {
        duckdb_destroy_result(&result);
        throw std::runtime_error("One or more required series (time, open, high, low, close) not found in PineVM.");
    }

    idx_t row_count = duckdb_row_count(&result);
    for (idx_t r = 0; r < row_count; ++r) {
        time_series->data.push_back(duckdb_value_double(&result, 0, r));
        date_series->data.push_back(duckdb_value_double(&result, 1, r));
        open_series->data.push_back(duckdb_value_double(&result, 2, r));
        high_series->data.push_back(duckdb_value_double(&result, 3, r));
        low_series->data.push_back(duckdb_value_double(&result, 4, r));
        close_series->data.push_back(duckdb_value_double(&result, 5, r));
    }

    duckdb_destroy_result(&result);
}

int CSVDataSource::getNumBars() const {
    return num_bars;
}