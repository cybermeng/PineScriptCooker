from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
import pybind11
import numpy
import sys

class CppBuildExt(build_ext):
    def build_extensions(self):
        # 增加一个打印语句，确认我们的自定义类正在运行
        print("\n--- Running Custom CppBuildExt ---")

        # 定义通用的C++编译选项
        cpp_opts = ['-std=c++17', '-O3', '-Wall', '-fPIC']
        # 定义链接选项，默认为空
        link_opts = []

        # 根据平台进行特定的设置
        if sys.platform == 'win32':
            print("Platform: Windows. Applying MinGW-specific options.")
            link_opts.extend(['-static-libgcc', '-static-libstdc++'])
        elif sys.platform.startswith('linux'):
            print("Platform: Linux. No special link options needed.")
        else:
            print(f"Platform: {sys.platform} (untested).")

        # 将设置应用到所有扩展模块
        for ext in self.extensions:
            ext.extra_compile_args = cpp_opts
            ext.extra_link_args = link_opts
            
            # --- 关键修复 ---
            # 在非Windows平台上，我们必须阻止构建系统生成和使用.def文件。
            # 这是Windows特有的概念，在Linux上会导致链接器错误。
            if sys.platform != 'win32':
                print(f"Disabling .def file for extension '{ext.name}' on {sys.platform}.")
                ext.def_file = None

        # 使用 super() 调用父类的 build_extensions 方法，让它用我们修改后的设置来完成工作
        super().build_extensions()

# 定义我们的 C++ 扩展模块
ext_modules = [
    Extension(
        # 'pine_vm.pine_vm_core' 意味着编译后的 .pyd/.so 文件会放在 pine_vm/ 目录下
        'pine_vm.pine_vm_core',
        # 更新源文件列表（路径分隔符使用正斜杠，跨平台兼容性更好）
        ['./bindings.cpp', 
         '../../Hithink/HithinkCompiler.cpp', 
         '../../Hithink/HithinkParser.cpp', # 假设Parser是Compiler的一部分
         '../../Hithink/HithinkLexer.cpp', # 假设Lexer是Parser的一部分
         '../../PineVM.cpp',
         '../../VMCommon.cpp' # 假设有这个文件
         ],
        # 包含目录
        include_dirs=[
            pybind11.get_include(),
            numpy.get_include(),
            '../../', # 包含项目源文件根目录，方便 #include "PineVM.h"
        ],
        language='c++',
    ),
]

setup(
    name='pine_vm',
    version='0.1.0',
    author='Your Name',
    author_email='your.email@example.com',
    description='A C++ accelerated technical analysis library for Python',
    long_description='',
    packages=['pine_vm'], # 指定 Python 包目录
    #package_dir={'': 'python'}, # 假设你的 pine_vm/__init__.py 在 python/pine_vm/__init__.py
    ext_modules=ext_modules, # 指定要编译的 C++ 扩展
    cmdclass={'build_ext': CppBuildExt}, # 使用我们的自定义构建类
    zip_safe=False,
    python_requires=">=3.7",
    install_requires=[
        "numpy",
        "pandas"
    ]
)