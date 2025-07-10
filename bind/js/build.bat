@echo off
REM ---------------------------------------------------------------
REM  Windows build script for PineVM WebAssembly module.
REM
REM  PREREQUISITE: You must run "emsdk_env.bat" in this command 
REM  prompt session before running this script. This sets up the
REM  necessary paths for the emcc compiler.
REM ---------------------------------------------------------------

echo Compiling PineVM to Wasm...

REM The main emcc command.
REM The caret (^) is the line continuation character in Windows Batch files.
emcc bindings.cpp ^
  -std=c++17 ^
  -o pine_vm.js ^
  -s WASM=1 ^
  -s MODULARIZE=1 ^
  -s EXPORT_ES6=1 ^
  -s "EXPORT_NAME='createPineVmModule'" ^
  -s ALLOW_MEMORY_GROWTH=1 ^
  --bind ^
  -O2

REM Check if the compilation was successful
IF %ERRORLEVEL% EQU 0 (
  echo.
  echo Compilation successful. Output: pine_vm.js, pine_vm.wasm
  echo To run the example server, execute: python run_server.py
) ELSE (
  echo.
  echo Compilation failed. Check the error messages above.
)

REM Pause the script so the user can see the output before the window closes.
pause