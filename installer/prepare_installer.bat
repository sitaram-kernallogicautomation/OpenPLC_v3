@echo off
REM Pre-build script to prepare files for the installer
REM This should be run on a Windows machine with MSYS2 before creating the installer

setlocal enabledelayedexpansion
set SCRIPT_DIR=%~dp0
set PROJECT_ROOT=%SCRIPT_DIR%..
set INSTALLER_DIR=%SCRIPT_DIR%
set RUNTIME_DIR=%INSTALLER_DIR%runtime

echo [Pre-Build] Preparing OpenPLC installer files...
echo.

REM Create runtime directory for pre-compiled files
if not exist "%RUNTIME_DIR%" mkdir "%RUNTIME_DIR%"

REM Check if we're in MSYS2 environment
if defined MSYSTEM (
    echo [Pre-Build] Detected MSYS2 environment. Compiling runtime...
    cd /d "%PROJECT_ROOT%"
    
    REM Install dependencies if needed
    echo [Pre-Build] Installing dependencies...
    pacman -S --noconfirm --needed git pkg-config automake autoconf libtool make sqlite3 python3 mingw-w64-x86_64-gcc mingw-w64-x86_64-pkg-config mingw-w64-x86_64-libmodbus mingw-w64-x86_64-open62541 2>nul
    
    REM Build MatIEC compiler
    echo [Pre-Build] Building MatIEC compiler...
    cd "%PROJECT_ROOT%\utils\matiec_src"
    if exist "iec2c.exe" (
        copy "iec2c.exe" "%RUNTIME_DIR%\" >nul
    ) else (
        echo [Pre-Build] Building MatIEC from source...
        autoreconf -i 2>nul
        ./configure 2>nul
        make 2>nul
        if exist "iec2c.exe" copy "iec2c.exe" "%RUNTIME_DIR%\" >nul
    )
    
    REM Build ST optimizer
    echo [Pre-Build] Building ST optimizer...
    cd "%PROJECT_ROOT%\utils\st_optimizer_src"
    g++ st_optimizer.cpp -o "%RUNTIME_DIR%\st_optimizer.exe" 2>nul
    
    REM Build glue generator
    echo [Pre-Build] Building glue generator...
    cd "%PROJECT_ROOT%\utils\glue_generator_src"
    g++ -std=c++11 glue_generator.cpp -o "%RUNTIME_DIR%\glue_generator.exe" 2>nul
    
    REM Compile OpenPLC runtime
    echo [Pre-Build] Compiling OpenPLC runtime...
    cd "%PROJECT_ROOT%\webserver"
    bash scripts/change_hardware_layer.sh blank
    bash scripts/compile_program.sh blank_program.st
    
    if exist "core\openplc.exe" (
        copy "core\openplc.exe" "%RUNTIME_DIR%\" >nul
        echo [Pre-Build] Runtime compiled successfully!
    ) else (
        echo [Pre-Build] WARNING: Runtime compilation failed. Installer will attempt compilation during installation.
    )
    
    REM Copy required DLLs
    echo [Pre-Build] Collecting required DLLs...
    REM Add DLL collection logic here based on your dependencies
    
) else (
    echo [Pre-Build] WARNING: Not in MSYS2 environment.
    echo [Pre-Build] Please run this script from MSYS2 MSYS terminal for full compilation.
    echo [Pre-Build] Or manually copy pre-compiled files to: %RUNTIME_DIR%
)

echo.
echo [Pre-Build] Preparation complete!
echo [Pre-Build] Files ready for installer in: %RUNTIME_DIR%
echo [Pre-Build] Next step: Compile the installer using Inno Setup Compiler
echo [Pre-Build] Open: %INSTALLER_DIR%OpenPLC_Installer.iss

endlocal
