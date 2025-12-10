@echo off
REM Start OpenPLC Runtime and Web Interface
REM This script starts the OpenPLC webserver

setlocal
set INSTALL_DIR=%~dp0
set WEBSERVER_DIR=%INSTALL_DIR%webserver

cd /d "%WEBSERVER_DIR%"

REM Activate Python virtual environment
if exist ".venv\Scripts\activate.bat" (
    call .venv\Scripts\activate.bat
) else (
    echo WARNING: Virtual environment not found. Using system Python.
)

REM Check if Python is available
python --version >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Python not found. Please install Python 3.8 or later.
    pause
    exit /b 1
)

echo Starting OpenPLC Runtime...
echo Web Interface will be available at: http://localhost:8080
echo.
echo Press Ctrl+C to stop the server.
echo.

REM Start the webserver
python webserver.py

endlocal
