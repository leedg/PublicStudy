@echo off
setlocal

set "CONFIG=Debug"
set "PLATFORM=x64"
set "SERVER_PORT=9000"
set "DB_HOST=127.0.0.1"
set "DB_PORT=8002"

if not "%~1"=="" set "CONFIG=%~1"
if not "%~2"=="" set "PLATFORM=%~2"
if not "%~3"=="" set "SERVER_PORT=%~3"
if not "%~4"=="" set "DB_HOST=%~4"
if not "%~5"=="" set "DB_PORT=%~5"

set "ROOT=%~dp0"
set "BIN=%ROOT%%PLATFORM%\%CONFIG%"
set "SERVER_EXE=%BIN%\TestServer.exe"

if not exist "%SERVER_EXE%" (
    echo Executable not found: %SERVER_EXE%
    exit /b 1
)

echo Starting server: %SERVER_EXE%
echo Listen port: %SERVER_PORT%
echo DB target: %DB_HOST%:%DB_PORT%
start "TestServer" /D "%BIN%" "%SERVER_EXE%" -p %SERVER_PORT% --db-host %DB_HOST% --db-port %DB_PORT%

endlocal
exit /b 0
