# CMake 最低版本要求。3.14 是一个比较现代且广泛可用的版本。
cmake_minimum_required(VERSION 3.14)

# 定义项目名称和所用语言 (C++)
project(PineVM CXX)

# -----------------------------------------------------------------------------
# 配置项目设置
# -----------------------------------------------------------------------------

# 我们需要 C++17 标准，因为代码中使用了 std::variant 等特性。
# 这是设置 C++ 标准的现代、可移植的方式。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # 推荐关闭编译器特定的扩展，以保证可移植性

# -----------------------------------------------------------------------------
# 查找并定义源文件
# -----------------------------------------------------------------------------

# 将项目的所有源文件 (.cpp) 收集到一个变量中，方便管理。
# 注意：CMake 会自动处理头文件 (.h) 的依赖关系，无需在此列出。
set(SOURCES
    main.cpp
    DataSource.cpp
    DataSource/JsonDataSource.cpp
    DataSource/CSVDataSource.cpp
    PineVM.cpp
    VMCommon.cpp

    PineScript/PineCompiler.cpp
    PineScript/PineParser.cpp
    PineScript/PineLexer.cpp
    # Add EasyLanguage source files
    EasyLanguage/EasyLanguageLexer.cpp
    EasyLanguage/EasyLanguageParser.cpp
    EasyLanguage/EasyLanguageCompiler.cpp

    # Add Hithink source files
    Hithink/HithinkLexer.cpp
    Hithink/HithinkParser.cpp
    Hithink/HithinkCompiler.cpp
)

# -----------------------------------------------------------------------------
# 定义可执行目标
# -----------------------------------------------------------------------------

# 从上面列出的源文件创建一个名为 "PineVM" 的可执行文件。
add_executable(PineVM ${SOURCES})

# -----------------------------------------------------------------------------
# 配置目标的属性
# -----------------------------------------------------------------------------

# 将当前目录添加到目标的包含路径中。
# 这是一个好习惯，可以确保 #include "PineVM.h" 等指令能被正确解析。
target_include_directories(PineVM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# -----------------------------------------------------------------------------
# 链接外部库 (DuckDB) - 跨平台优化版本
# -----------------------------------------------------------------------------

# 我们使用 Imported Target 的方式来处理 DuckDB 库，这是现代 CMake 的推荐做法。
# 它将库的实际文件路径抽象为一个目标，使得链接代码平台无关。
add_library(duckdb SHARED IMPORTED GLOBAL)

# 根据不同的操作系统，设置 duckdb 目标的不同属性。
if(WIN32)
    # 在 Windows 上，我们需要动态链接库 (.dll) 和导入库 (.lib)
    set(DUCKDB_SHARED_LIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/duckdb.dll")
    set(DUCKDB_IMPORT_LIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/duckdb.lib")

    # 检查文件是否存在
    if(NOT EXISTS "${DUCKDB_SHARED_LIB_PATH}" OR NOT EXISTS "${DUCKDB_IMPORT_LIB_PATH}")
        message(FATAL_ERROR "DuckDB 库文件 (duckdb.dll/duckdb.lib) 未在项目根目录找到。")
    endif()

    # 设置 Imported Target 的属性
    set_target_properties(duckdb PROPERTIES
        IMPORTED_LOCATION "${DUCKDB_SHARED_LIB_PATH}"
        IMPORTED_IMPLIB "${DUCKDB_IMPORT_LIB_PATH}"
    )

elseif(UNIX AND NOT APPLE)
    # 在 Linux 上，我们只需要共享对象文件 (.so)
    set(DUCKDB_SHARED_LIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/libduckdb.so")

    if(NOT EXISTS "${DUCKDB_SHARED_LIB_PATH}")
        message(FATAL_ERROR "DuckDB 库文件 (libduckdb.so) 未在项目根目录找到。")
    endif()

    set_target_properties(duckdb PROPERTIES
        IMPORTED_LOCATION "${DUCKDB_SHARED_LIB_PATH}"
    )

elseif(APPLE)
    # 在 macOS 上，我们只需要动态库文件 (.dylib)
    set(DUCKDB_SHARED_LIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/libduckdb.dylib")

    if(NOT EXISTS "${DUCKDB_SHARED_LIB_PATH}")
        message(FATAL_ERROR "DuckDB 库文件 (libduckdb.dylib) 未在项目根目录找到。")
    endif()

    set_target_properties(duckdb PROPERTIES
        IMPORTED_LOCATION "${DUCKDB_SHARED_LIB_PATH}"
    )
else()
    message(FATAL_ERROR "不支持的操作系统平台。")
endif()


# 将 DuckDB 库链接到我们的可执行文件。
# 因为我们使用了 Imported Target，所以这里只需要写目标名 "duckdb" 即可，CMake 会自动处理。
target_link_libraries(PineVM PRIVATE duckdb)

# 添加一个构建后命令，将 DuckDB 的动态库复制到可执行文件所在的目录。
# 这对于在所有平台上方便地运行程序都是必需的。
# ${DUCKDB_SHARED_LIB_PATH} 变量已经在上面的 if/else 块中被正确设置。
add_custom_command(
    TARGET PineVM POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${DUCKDB_SHARED_LIB_PATH}"
        "$<TARGET_FILE_DIR:PineVM>"
    COMMENT "Copying DuckDB shared library to build directory")

# 打印一条消息，方便用户知道配置已完成。
message(STATUS "配置完成。现在你可以构建 'PineVM' 目标了。")