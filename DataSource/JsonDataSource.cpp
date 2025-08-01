#include "JsonDataSource.h"
#include "../PineVM.h" // For PineVM definition
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

    // 1. Get row count from the newline-delimited JSON file.
    std::string count_query = "SELECT count(*) FROM read_json_auto('" + file_path + "', format='newline_delimited')";
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

    // 2. Create a clean, structured table from the complex JSON source.
    // This query does all the heavy lifting:
    //  - Reads the file as newline-delimited JSON.
    //  - Accesses the nested time value using `time."$date"`. Note the quotes around "$date".
    //  - Casts the ISO 8601 date string to a proper TIMESTAMP.
    //  - Selects the data columns by their quoted numeric names ("7", "8", etc.).
    //  - Casts the data columns to DOUBLE to ensure correct type.
    //  - Renames all columns to standard names (time, open, high, etc.) for easy access later.
    // Using a raw string literal R"(...)" makes the SQL much cleaner to write in C++.
    std::string create_table_query = R"(
        CREATE TABLE market_data AS SELECT
            CAST(time."$date" AS TIMESTAMP) AS time,
            CAST("7" AS DOUBLE) AS open,
            CAST("8" AS DOUBLE) AS high,
            CAST("9" AS DOUBLE) AS low,
            CAST("11" AS DOUBLE) AS close,
            CAST("13" AS DOUBLE) AS volume,
            CAST("19" AS DOUBLE) AS amount
        FROM read_json_auto(')" + file_path + R"(', format='newline_delimited')
    )";

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
    // Because initialize() created a clean table, this query is simple and standard.
    // It reads from the 'market_data' table which now has standard column names and types.
    std::string query = "SELECT epoch(time), strftime(time, '%Y%m%d'), open, high, low, close, volume, amount FROM market_data ORDER BY time ASC";
    
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
    vm.registerSeries("volume", std::make_shared<Series>());
    vm.registerSeries("amount", std::make_shared<Series>());

    auto* time_series = vm.getSeries("time");
    auto* date_series = vm.getSeries("date");
    auto* open_series = vm.getSeries("open");
    auto* high_series = vm.getSeries("high");
    auto* low_series = vm.getSeries("low");
    auto* close_series = vm.getSeries("close");
    auto* volume_series = vm.getSeries("volume");
    auto* amount_series = vm.getSeries("amount");

    if (!time_series || !date_series || !open_series || !high_series || !low_series || !close_series || !volume_series || !amount_series) {
        duckdb_destroy_result(&result);
        throw std::runtime_error("One or more required series (open, high, low, close, volume) not found in PineVM.");
    }

    idx_t row_count = duckdb_row_count(&result);
    for (idx_t r = 0; r < row_count; ++r) {
        time_series->data.push_back(duckdb_value_double(&result, 0, r)); // epoch(time)
        date_series->data.push_back(duckdb_value_double(&result, 1, r));
        open_series->data.push_back(duckdb_value_double(&result, 2, r));
        high_series->data.push_back(duckdb_value_double(&result, 3, r));
        low_series->data.push_back(duckdb_value_double(&result, 4, r));
        close_series->data.push_back(duckdb_value_double(&result, 5, r));
        volume_series->data.push_back(duckdb_value_double(&result, 6, r));
        amount_series->data.push_back(duckdb_value_double(&result, 7, r));
    }

    duckdb_destroy_result(&result);
}

int JsonDataSource::getNumBars() const {
    return num_bars;
}