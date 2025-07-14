@echo off
setlocal

:: build.bat - Compiles the PineVM C++ project to WebAssembly using Emscripten on Windows.

:: --- Configuration ---
:: C++ source files to compile
set SOURCES=main.cpp ../../PineVM.cpp

:: Output directory for build artifacts
set OUTPUT_DIR=public

:: Final JavaScript output file (will also generate a .wasm file)
set OUTPUT_JS=%OUTPUT_DIR%/pine_vm.js

:: Source HTML file
set SOURCE_HTML=index.html

:: Emscripten compiler flags. Note: No array syntax in batch.
set EMCC_FLAGS=-std=c++17 -gsource-map -sSAFE_HEAP=1 -sASSERTIONS=1 -O3 -fexceptions -s ALLOW_MEMORY_GROWTH=1 -s MODULARIZE=1 -s EXPORT_ES6=1 -s EXPORT_NAME="createPineVmModule" -s ENVIRONMENT=web -s WASM=1 -s EXPORTED_FUNCTIONS="['_run_pine_calculation', '_malloc', '_free', '_realloc', 'stringToUTF8']" -s EXPORTED_RUNTIME_METHODS="['cwrap']"
:: --- Build Process ---
echo --- Starting PineVM WebAssembly Build ---

:: Check if emcc command is available
where emcc >nul 2>nul
if %errorlevel% neq 0 (
    echo.
    echo ERROR: 'emcc' command not found.
    echo Please make sure the Emscripten SDK is installed and its environment is active.
    echo You can activate it by running 'emsdk_env.bat' in your command prompt.
    echo.
    exit /b 1
)

:: 1. Create the output directory if it doesn't exist
echo.
echo => Creating output directory: %OUTPUT_DIR%
if not exist "%OUTPUT_DIR%" (
    mkdir "%OUTPUT_DIR%"
)

:: 2. Compile C++ sources to JavaScript and WebAssembly
echo.
echo => Compiling C++ sources...
emcc %EMCC_FLAGS% %SOURCES% -o "%OUTPUT_JS%"
if %errorlevel% neq 0 (
    echo.
    echo ERROR: Compilation failed.
    exit /b 1
)
echo => Compilation successful! Generated:
echo    - %OUTPUT_JS%
echo    - %OUTPUT_JS:.js=.wasm%

:: 3. Copy support files to the output directory
echo.
echo => Copying support files to %OUTPUT_DIR%
copy "%SOURCE_HTML%" "%OUTPUT_DIR%\" >nul
copy "main.js" "%OUTPUT_DIR%\" >nul


echo.
echo --- Build Complete! ---
echo All necessary files are in the '%OUTPUT_DIR%' directory.
echo.
echo To run the application, start a local web server.
echo For example, using Python:
echo cd %OUTPUT_DIR% ^& python -m http.server
echo.
echo Then, open your browser to http://localhost:8000
echo.

endlocal