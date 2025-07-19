from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
import pybind11
import numpy
import sys

# The build class is now much simpler.
# Its only job is to add specific flags for MinGW.
class CppBuildExt(build_ext):
    def build_extensions(self):
        # These flags are for MinGW
        cpp_opts = ['-std=c++17', '-O3', '-Wall', '-fPIC']
        link_opts = ['-static-libgcc', '-static-libstdc++']

        for ext in self.extensions:
            ext.extra_compile_args = cpp_opts
            ext.extra_link_args = link_opts

        # Let the parent class do the heavy lifting
        build_ext.build_extensions(self)

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