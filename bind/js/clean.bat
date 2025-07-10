@echo off
setlocal

:: clean.bat - Removes the build artifacts directory.

set OUTPUT_DIR=public

if exist "%OUTPUT_DIR%" (
    echo Removing directory: %OUTPUT_DIR%
    :: /S for subdirectories, /Q for quiet mode (no confirmation)
    rmdir /S /Q "%OUTPUT_DIR%"
    echo Cleanup complete.
) else (
    echo Directory %OUTPUT_DIR% does not exist. Nothing to clean.
)

endlocal