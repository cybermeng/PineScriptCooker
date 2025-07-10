#!/bin/bash

# 确保 Emscripten 环境变量已设置
# source /path/to/emsdk/emsdk_env.sh

echo "Compiling PineVM to Wasm..."

emcc bindings.cpp \
  -std=c++17 \
  -o pine_vm.js \
  -s WASM=1 \
  -s MODULARIZE=1 \
  -s EXPORT_ES6=1 \
  -s 'EXPORT_NAME="createPineVmModule"' \
  -s ALLOW_MEMORY_GROWTH=1 \
  --bind \
  -O2

if [ $? -eq 0 ]; then
  echo "Compilation successful. Output: pine_vm.js, pine_vm.wasm"
  echo "To run the example: node index.js"
else
  echo "Compilation failed."
fi