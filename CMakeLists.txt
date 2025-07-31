# cmake .. -G "Ninja"
# CMake 最低版本要求。3.14 是一个比较现代且广泛可用的版本。
cmake_minimum_required(VERSION 3.14)

# 定义项目名称和所用语言 (C++)
project(PineVM CXX)

# -----------------------------------------------------------------------------
# 配置项目设置
# -----------------------------------------------------------------------------

# 我们需要 C++17 标准，因为代码中使用了 std::variant 等特性。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -----------------------------------------------------------------------------
# 引入构建选项 (用于开启/关闭测试)
# -----------------------------------------------------------------------------
option(BUILD_TESTING "Build the project's tests" ON)

# -----------------------------------------------------------------------------
# 查找并定义源文件 - 重构为核心库
# -----------------------------------------------------------------------------

# 将项目的所有共享逻辑源文件 (.cpp) 收集到一个变量中。
# main.cpp 和 main_test.cpp 是程序的入口点，不属于共享库。
set(PINEVM_CORE_SOURCES
    DataSource.cpp
    DataSource/JsonDataSource.cpp
    DataSource/CSVDataSource.cpp
    PineVM.cpp
    VMCommon.cpp
    VMFunc.cpp

    PineScript/PineCompiler.cpp
    PineScript/PineParser.cpp
    PineScript/PineLexer.cpp
    
    EasyLanguage/EasyLanguageLexer.cpp
    EasyLanguage/EasyLanguageParser.cpp
    EasyLanguage/EasyLanguageCompiler.cpp

    Hithink/HithinkLexer.cpp
    Hithink/HithinkParser.cpp
    Hithink/HithinkCompiler.cpp
)

# 创建一个静态库来包含所有核心逻辑。
# 主程序和测试程序都将链接到这个库，避免了代码重复。
add_library(PineVMCore STATIC ${PINEVM_CORE_SOURCES})

# 将当前目录添加到核心库的包含路径中。
# PUBLIC 意味着链接到 PineVMCore 的任何目标都会自动继承这个包含目录。
target_include_directories(PineVMCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})


# -----------------------------------------------------------------------------
# 定义主可执行目标
# -----------------------------------------------------------------------------

# 从 main.cpp 创建名为 "PineVM" 的主可执行文件。
add_executable(PineVM main.cpp)

# 将核心库链接到主程序。
target_link_libraries(PineVM PRIVATE PineVMCore)


# -----------------------------------------------------------------------------
# 链接外部库 (DuckDB) - 仅为主程序链接
# -----------------------------------------------------------------------------

# 我们使用 Imported Target 的方式来处理 DuckDB 库
add_library(duckdb SHARED IMPORTED GLOBAL)

if(WIN32)
    set(DUCKDB_SHARED_LIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/duckdb.dll")
    set(DUCKDB_IMPORT_LIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/duckdb.lib")
    if(NOT EXISTS "${DUCKDB_SHARED_LIB_PATH}" OR NOT EXISTS "${DUCKDB_IMPORT_LIB_PATH}")
        message(FATAL_ERROR "DuckDB 库文件 (duckdb.dll/duckdb.lib) 未在项目根目录找到。")
    endif()
    set_target_properties(duckdb PROPERTIES IMPORTED_LOCATION "${DUCKDB_SHARED_LIB_PATH}" IMPORTED_IMPLIB "${DUCKDB_IMPORT_LIB_PATH}")
elseif(UNIX AND NOT APPLE)
    set(DUCKDB_SHARED_LIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/libduckdb.so")
    if(NOT EXISTS "${DUCKDB_SHARED_LIB_PATH}")
        message(FATAL_ERROR "DuckDB 库文件 (libduckdb.so) 未在项目根目录找到。")
    endif()
    set_target_properties(duckdb PROPERTIES IMPORTED_LOCATION "${DUCKDB_SHARED_LIB_PATH}")
elseif(APPLE)
    set(DUCKDB_SHARED_LIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/libduckdb.dylib")
    if(NOT EXISTS "${DUCKDB_SHARED_LIB_PATH}")
        message(FATAL_ERROR "DuckDB 库文件 (libduckdb.dylib) 未在项目根目录找到。")
    endif()
    set_target_properties(duckdb PROPERTIES IMPORTED_LOCATION "${DUCKDB_SHARED_LIB_PATH}")
else()
    message(FATAL_ERROR "不支持的操作系统平台。")
endif()

# 将 DuckDB 库链接到我们的主可执行文件。
target_link_libraries(PineVM PRIVATE duckdb)

# 添加构建后命令，将 DuckDB 的动态库复制到主可执行文件所在的目录。
add_custom_command(
    TARGET PineVM POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${DUCKDB_SHARED_LIB_PATH}"
        "$<TARGET_FILE_DIR:PineVM>"
    COMMENT "Copying DuckDB shared library to build directory")


# -----------------------------------------------------------------------------
# 定义测试目标 (如果开启了 BUILD_TESTING)
# -----------------------------------------------------------------------------
if(BUILD_TESTING)
    # 启用 CMake 的测试功能 (CTest)
    enable_testing()

    # 从 main_test.cpp 创建一个名为 "pinevm_tester" 的可执行文件
    add_executable(pinevm_tester ./tests/main_test.cpp)

    # 将核心库链接到测试程序
    # 注意：测试程序不需要链接 DuckDB
    target_link_libraries(pinevm_tester PRIVATE PineVMCore)

    # 将测试添加到 CTest
    # 这样就可以通过运行 "ctest" 命令来执行测试了
    add_test(NAME HithinkFunctionTests COMMAND pinevm_tester)
    
    message(STATUS "测试已启用。将构建 'pinevm_tester' 目标。")
endif()

# -----------------------------------------------------------------------------

message(STATUS "配置完成。现在你可以构建项目了。")