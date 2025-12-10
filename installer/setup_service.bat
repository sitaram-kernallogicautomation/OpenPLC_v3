@echo off
REM Windows Service installation script for OpenPLC
REM This script installs OpenPLC as a Windows Service using NSSM (Non-Sucking Service Manager)

setlocal
set INSTALL_DIR=%~dp0
set WEBSERVER_DIR=%INSTALL_DIR%webserver
set SERVICE_NAME=OpenPLC

REM Check if running as administrator
net session >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: This script must be run as Administrator.
    echo Right-click and select "Run as administrator"
    pause
    exit /b 1
)

REM Check if service already exists
sc query "%SERVICE_NAME%" >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo Service %SERVICE_NAME% already exists. Stopping and removing...
    sc stop "%SERVICE_NAME%"
    timeout /t 2 /nobreak >nul
    sc delete "%SERVICE_NAME%"
    timeout /t 2 /nobreak >nul
)

REM Determine Python executable
set PYTHON_EXE=
if exist "%WEBSERVER_DIR%\.venv\Scripts\python.exe" (
    set PYTHON_EXE=%WEBSERVER_DIR%\.venv\Scripts\python.exe
) else (
    where python >nul 2>&1
    if %ERRORLEVEL% EQU 0 (
        for /f "delims=" %%i in ('where python') do set PYTHON_EXE=%%i
    ) else (
        echo ERROR: Python not found. Please ensure Python is installed.
        pause
        exit /b 1
    )
)

REM Check if NSSM is available (bundled or in PATH)
set NSSM_EXE=
if exist "%INSTALL_DIR%tools\nssm.exe" (
    set NSSM_EXE=%INSTALL_DIR%tools\nssm.exe
) else (
    where nssm >nul 2>&1
    if %ERRORLEVEL% EQU 0 (
        for /f "delims=" %%i in ('where nssm') do set NSSM_EXE=%%i
    )
)

if "%NSSM_EXE%"=="" (
    echo [OpenPLC Service] NSSM not found. Installing service using sc command...
    echo [OpenPLC Service] Note: For better service management, consider installing NSSM.
    
    REM Create a wrapper batch file for the service
    set WRAPPER_BAT=%INSTALL_DIR%start_service.bat
    (
        echo @echo off
        echo cd /d "%WEBSERVER_DIR%"
        echo call .venv\Scripts\activate.bat
        echo python webserver.py
    ) > "%WRAPPER_BAT%"
    
    REM Install using sc (basic Windows service)
    sc create "%SERVICE_NAME%" binPath= "\"%WRAPPER_BAT%\"" start= auto
    sc description "%SERVICE_NAME%" "OpenPLC Runtime - Industrial PLC Runtime System"
    sc config "%SERVICE_NAME%" start= auto
    goto :service_installed
)

REM Install service using NSSM (recommended)
echo [OpenPLC Service] Installing service using NSSM...
"%NSSM_EXE%" install "%SERVICE_NAME%" "%PYTHON_EXE%" "%WEBSERVER_DIR%\webserver.py"
"%NSSM_EXE%" set "%SERVICE_NAME%" AppDirectory "%WEBSERVER_DIR%"
"%NSSM_EXE%" set "%SERVICE_NAME%" DisplayName "OpenPLC Runtime"
"%NSSM_EXE%" set "%SERVICE_NAME%" Description "OpenPLC Runtime - Industrial PLC Runtime System"
"%NSSM_EXE%" set "%SERVICE_NAME%" Start SERVICE_AUTO_START
"%NSSM_EXE%" set "%SERVICE_NAME%" AppStdout "%WEBSERVER_DIR%\service_stdout.log"
"%NSSM_EXE%" set "%SERVICE_NAME%" AppStderr "%WEBSERVER_DIR%\service_stderr.log"

:service_installed
echo [OpenPLC Service] Starting service...
sc start "%SERVICE_NAME%"
timeout /t 2 /nobreak >nul

sc query "%SERVICE_NAME%" | find "RUNNING" >nul
if %ERRORLEVEL% EQU 0 (
    echo [OpenPLC Service] Service installed and started successfully!
    echo [OpenPLC Service] Access OpenPLC at: http://localhost:8080
) else (
    echo [OpenPLC Service] Service installed but failed to start.
    echo [OpenPLC Service] Check service logs for details.
)

endlocal
