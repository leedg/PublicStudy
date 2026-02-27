@echo off
setlocal

set "CONFIG=Debug"
set "PLATFORM=x64"
set "DB_PORT=8002"

if not "%~1"=="" set "CONFIG=%~1"
if not "%~2"=="" set "PLATFORM=%~2"
if not "%~3"=="" set "DB_PORT=%~3"

set "ROOT=%~dp0"
set "BIN=%ROOT%%PLATFORM%\%CONFIG%"
set "DB_EXE=%BIN%\TestDBServer.exe"

if not exist "%DB_EXE%" (
    echo Executable not found: %DB_EXE%
    exit /b 1
)

echo Starting DB server: %DB_EXE%
echo Port: %DB_PORT%
start "TestDBServer" /D "%BIN%" "%DB_EXE%" -p %DB_PORT%

endlocal
exit /b 0
