@echo off
REM Post-installation script for OpenPLC Runtime
REM This script sets up the Python environment and compiles the runtime

setlocal enabledelayedexpansion
set INSTALL_DIR=%~dp0
set WEBSERVER_DIR=%INSTALL_DIR%webserver
set PYTHON_CMD=python

REM Check if Python is available
where python >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    REM Try bundled Python
    if exist "%INSTALL_DIR%python\python.exe" (
        set PYTHON_CMD=%INSTALL_DIR%python\python.exe
        set PATH=%INSTALL_DIR%python;%INSTALL_DIR%python\Scripts;%PATH%
    ) else (
        echo ERROR: Python not found. Please install Python 3.8 or later.
        echo Download from: https://www.python.org/downloads/
        pause
        exit /b 1
    )
)

echo [OpenPLC Installer] Setting up Python environment...
cd /d "%WEBSERVER_DIR%"

REM Create virtual environment if it doesn't exist
if not exist ".venv" (
    echo [OpenPLC Installer] Creating Python virtual environment...
    "%PYTHON_CMD%" -m venv .venv
)

REM Install Python dependencies
echo [OpenPLC Installer] Installing Python dependencies...
call .venv\Scripts\activate.bat
python -m pip install --upgrade pip --quiet
python -m pip install flask==2.3.3 werkzeug==2.3.7 flask-login==0.6.2 pyserial pymodbus==2.5.3 flask_jwt_extended flask_sqlalchemy python-dotenv --quiet

REM Check if runtime is already compiled
if exist "core\openplc.exe" (
    echo [OpenPLC Installer] Runtime executable found, skipping compilation.
    goto :setup_complete
)

REM Check if compilation tools are available
if not exist "iec2c.exe" (
    echo [OpenPLC Installer] WARNING: iec2c.exe not found. Runtime compilation skipped.
    echo [OpenPLC Installer] Please compile the runtime manually or use pre-compiled version.
    goto :setup_complete
)

REM Compile blank program to create runtime (if not pre-compiled)
if not exist "core\openplc.exe" (
    echo [OpenPLC Installer] Runtime not found. Attempting compilation...
    if exist "scripts\compile_program_windows.bat" (
        REM Use Windows-native batch script
        call scripts\compile_program_windows.bat blank_program.st
    ) else if exist "scripts\compile_program.sh" (
        REM For MSYS2/Cygwin environment (if available)
        where bash >nul 2>&1
        if %ERRORLEVEL% EQU 0 (
            bash scripts\compile_program.sh blank_program.st
        ) else (
            echo [OpenPLC Installer] WARNING: Bash not found. Cannot compile runtime.
            echo [OpenPLC Installer] Please ensure runtime is pre-compiled or install MSYS2.
        )
    ) else (
        echo [OpenPLC Installer] WARNING: Compilation script not found.
        echo [OpenPLC Installer] Runtime should be pre-compiled in core\openplc.exe
    )
) else (
    echo [OpenPLC Installer] Pre-compiled runtime found, skipping compilation.
)

:setup_complete
echo [OpenPLC Installer] Setup complete!
echo [OpenPLC Installer] OpenPLC Runtime is ready to use.
echo [OpenPLC Installer] Access the web interface at: http://localhost:8080

endlocal
