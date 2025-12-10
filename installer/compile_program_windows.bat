@echo off
REM Windows-native compilation script for OpenPLC programs
REM This is an alternative to compile_program.sh for Windows environments without bash

setlocal enabledelayedexpansion
set WEBSERVER_DIR=%~dp0..
set CORE_DIR=%WEBSERVER_DIR%\core
set SCRIPTS_DIR=%WEBSERVER_DIR%\scripts

if "%~1"=="" (
    echo Error: You must provide a file to be compiled as argument
    exit /b 1
)

set ST_FILE=%~1

cd /d "%SCRIPTS_DIR%"

REM Read platform and driver settings
set OPENPLC_PLATFORM=win
set OPENPLC_DRIVER=blank
set ETHERCAT_OPT=

if exist "openplc_platform" (
    set /p OPENPLC_PLATFORM=<openplc_platform
)
if exist "openplc_driver" (
    set /p OPENPLC_DRIVER=<openplc_driver
)
if exist "ethercat" (
    set /p ETHERCAT_OPT=<ethercat
)

REM Store active program
echo %ST_FILE% > ..\active_program

REM Compile ST to C
cd /d "%WEBSERVER_DIR%"
echo Generating C files...
if exist "iec2c.exe" (
    iec2c.exe -f -l -p -r -R -a st_files\%ST_FILE%
) else if exist "iec2c" (
    iec2c -f -l -p -r -R -a st_files\%ST_FILE%
) else (
    echo ERROR: iec2c compiler not found!
    exit /b 1
)

if %ERRORLEVEL% NEQ 0 (
    echo Error generating C files
    echo Compilation finished with errors!
    exit /b 1
)

REM Add EtherCAT include if needed
if "%ETHERCAT_OPT%"=="ethercat" (
    REM Note: sed equivalent for Windows would need to be installed
    echo WARNING: EtherCAT support requires sed or manual editing
)

REM Copy snap7 files
echo Including Siemens S7 Protocol via snap7...
if exist "..\utils\snap7_src\wrapper\oplc_snap7.cpp" (
    copy /Y "..\utils\snap7_src\wrapper\oplc_snap7.*" "%CORE_DIR%\" >nul
)

REM Move generated files to core
echo Moving files...
move /Y POUS.c "%CORE_DIR%\" >nul
move /Y POUS.h "%CORE_DIR%\" >nul
move /Y LOCATED_VARIABLES.h "%CORE_DIR%\" >nul
move /Y VARIABLES.csv "%CORE_DIR%\" >nul
move /Y Config0.c "%CORE_DIR%\" >nul
move /Y Config0.h "%CORE_DIR%\" >nul
move /Y Res0.c "%CORE_DIR%\" >nul

if %ERRORLEVEL% NEQ 0 (
    echo Error moving files
    echo Compilation finished with errors!
    exit /b 1
)

REM Compile for Windows
cd /d "%CORE_DIR%"
echo Compiling for Windows...
echo Generating object files...

REM Compile Config0.c
g++ -I .\lib -c Config0.c -w
if %ERRORLEVEL% NEQ 0 (
    echo Error compiling Config0.c
    echo Compilation finished with errors!
    exit /b 1
)

REM Compile Res0.c
g++ -I .\lib -c Res0.c -w
if %ERRORLEVEL% NEQ 0 (
    echo Error compiling Res0.c
    echo Compilation finished with errors!
    exit /b 1
)

REM Generate glueVars
echo Generating glueVars...
if exist "glue_generator.exe" (
    glue_generator.exe
) else if exist "glue_generator" (
    glue_generator
) else (
    echo WARNING: glue_generator not found, skipping...
)

REM Compile main program
echo Compiling main program...
REM Note: Adjust library paths based on your MSYS2/MinGW installation
g++ *.cpp *.o -o openplc.exe -I .\lib -pthread -fpermissive -I /usr/local/include/modbus -L /usr/local/lib snap7.lib -lmodbus -w

if %ERRORLEVEL% NEQ 0 (
    echo Error compiling main program
    echo Compilation finished with errors!
    exit /b 1
)

echo Compilation finished successfully!
exit /b 0
