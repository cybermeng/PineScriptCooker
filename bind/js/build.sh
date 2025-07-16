#!/bin/bash

# build.sh - Compiles the PineVM C++ project to WebAssembly using Emscripten.

# Stop the script if any command fails
set -e

# --- Configuration ---
# C++ source files to compile
SOURCES="main.cpp ../../PineVM.cpp"

# Output directory for build artifacts
OUTPUT_DIR="public"

# Final JavaScript output file (will also generate a .wasm file)
OUTPUT_JS="$OUTPUT_DIR/pine_vm.js"

# Source HTML file
SOURCE_HTML="index.html"

# Emscripten compiler flags
# -std=c++17: Use the C++17 standard.
# -O3: Aggressive optimization for speed.
# -lembind: Link the embind library for C++/JavaScript bindings.
# -s ALLOW_MEMORY_GROWTH=1: Allows the WebAssembly memory to grow if needed.
# -s MODULARIZE=1: Wraps the output in a JavaScript module.
# -s EXPORT_ES6=1: Use ES6 module format (import/export).
# -s EXPORT_NAME="createPineVmModule": The name of the factory function to load the module.
# -s ENVIRONMENT=web: Target the web browser environment.
# -s WASM=1: Ensure WASM output is generated.
EMCC_FLAGS=(
    -std=c++17
    -O3
    # 启用 C++ 异常处理，这是捕获 std::runtime_error 的关键
    -fexceptions
    -gsource-map
    -s SAFE_HEAP=1
    -s ASSERTIONS=1
    -s ALLOW_MEMORY_GROWTH=1
    -s MODULARIZE=1
    -s EXPORT_ES6=1
    -s EXPORT_NAME="createPineVmModule"
    -s ENVIRONMENT=web
    -s WASM=1
    -s EXPORTED_FUNCTIONS="['_run_pine_calculation', '_malloc', '_free', '_realloc', 'stringToUTF8']"
    -s EXPORTED_RUNTIME_METHODS="['cwrap']"
)

# --- Build Process ---
# ANSI color codes for better output
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

echo -e "${YELLOW}--- Starting PineVM WebAssembly Build ---${NC}"

# 1. Create the output directory if it doesn't exist
echo "=> Creating output directory: $OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"

# 2. Compile C++ sources to JavaScript and WebAssembly
echo "=> Compiling C++ sources..."
emcc "${EMCC_FLAGS[@]}" $SOURCES -o "$OUTPUT_JS"
echo -e "${GREEN}=> Compilation successful!${NC} Generated:"
echo "   - $OUTPUT_JS"
echo "   - ${OUTPUT_JS%.js}.wasm"

# 3. Copy support files to the output directory
echo "=> Copying support files to $OUTPUT_DIR"
cp "$SOURCE_HTML" "$OUTPUT_DIR/"
cp "main.js" "$OUTPUT_DIR/"


echo -e "\n${GREEN}--- Build Complete! ---${NC}"
echo -e "All necessary files are in the '${YELLOW}$OUTPUT_DIR${NC}' directory."
echo -e "\nTo run the application, start a local web server."
echo -e "For example, using Python:"
echo -e "${YELLOW}cd $OUTPUT_DIR && python3 -m http.server${NC}"
echo -e "Then, open your browser to ${GREEN}http://localhost:8000${NC}"