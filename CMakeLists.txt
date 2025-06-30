# CMake 最低版本要求。3.14 是一个比较现代且广泛可用的版本。
cmake_minimum_required(VERSION 3.14)

# 定义项目名称和所用语言 (C++)
project(PineCompilerVM CXX)

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
    PineVM.cpp
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

# 从上面列出的源文件创建一个名为 "PineCompilerVM" 的可执行文件。
add_executable(PineCompilerVM ${SOURCES})

# -----------------------------------------------------------------------------
# 配置目标的属性
# -----------------------------------------------------------------------------

# 将当前目录添加到目标的包含路径中。
# 这是一个好习惯，可以确保 #include "PineVM.h" 等指令能被正确解析。
target_include_directories(PineCompilerVM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# -----------------------------------------------------------------------------
# 链接外部库 (DuckDB)
# -----------------------------------------------------------------------------

# 指定 DuckDB 库文件的路径。
# 我们假设 duckdb.lib 和 duckdb.dll 位于项目的根目录。
set(DUCKDB_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/duckdb.lib)
set(DUCKDB_DLL ${CMAKE_CURRENT_SOURCE_DIR}/duckdb.dll)

# 检查库文件是否存在，如果不存在则给出错误提示。
if(NOT EXISTS ${DUCKDB_LIBRARY})
    message(FATAL_ERROR "DuckDB library not found at ${DUCKDB_LIBRARY}")
endif()
if(NOT EXISTS ${DUCKDB_DLL})
    message(FATAL_ERROR "DuckDB DLL not found at ${DUCKDB_DLL}")
endif()

# 将 DuckDB 库链接到我们的可执行文件。
target_link_libraries(PineCompilerVM PRIVATE ${DUCKDB_LIBRARY})

# 添加一个构建后命令，将 duckdb.dll 复制到可执行文件所在的目录。
# 这对于在 Windows 上运行程序是必需的。
add_custom_command(
    TARGET PineCompilerVM POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${DUCKDB_DLL}"
    "$<TARGET_FILE_DIR:PineCompilerVM>"
    COMMENT "Copying duckdb.dll to build directory")

# 打印一条消息，方便用户知道配置已完成。
message(STATUS "配置完成。现在你可以构建 'PineCompilerVM' 目标了。")