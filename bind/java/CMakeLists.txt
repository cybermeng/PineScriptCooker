cmake_minimum_required(VERSION 3.10)
project(pinevm_jni CXX)

# 设置 C++ 标准为 C++17，因为代码中用到了
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 查找 JNI (Java Native Interface)
# 这会定义 JNI_INCLUDE_DIRS 和 JNI_LIBRARIES 变量
find_package(JNI REQUIRED)

# 添加一个共享库目标，这是 Java 将要加载的文件
add_library(pinevm_jni SHARED
    # 列出所有需要编译的 C++源文件
    ../../PineVM.cpp
    ../../VMCommon.cpp
    ../../Hithink/HithinkCompiler.cpp
    jniexport.cpp
)

# 将 Hithink AST 的头文件目录包含进来
# 假设 HithinkAST.h 在 HithinkCompiler.h 的同级目录
# 如果不在，你需要调整路径
target_include_directories(pinevm_jni PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 链接 JNI 库
if(JNI_LIBRARIES)
    target_link_libraries(pinevm_jni PRIVATE ${JNI_LIBRARIES})
endif()

# 包含 JNI 头文件目录
if(JNI_INCLUDE_DIRS)
    target_include_directories(pinevm_jni PRIVATE ${JNI_INCLUDE_DIRS})
endif()

# (可选) 安装指令，用于将编译好的库文件安装到指定位置
# install(TARGETS pinevm_jni DESTINATION lib)

# 打印找到的 JNI 路径，方便调试
message(STATUS "JNI include directories: ${JNI_INCLUDE_DIRS}")
message(STATUS "JNI libraries: ${JNI_LIBRARIES}")