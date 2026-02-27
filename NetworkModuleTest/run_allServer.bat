@echo off
setlocal

set "CONFIG=Debug"
set "PLATFORM=x64"
set "SERVER_PORT=9000"
set "DB_PORT=8002"
set "DB_HOST=127.0.0.1"

if not "%~1"=="" set "CONFIG=%~1"
if not "%~2"=="" set "PLATFORM=%~2"
if not "%~3"=="" set "SERVER_PORT=%~3"
if not "%~4"=="" set "DB_PORT=%~4"
if not "%~5"=="" set "DB_HOST=%~5"

set "ROOT=%~dp0"

call "%ROOT%run_dbServer.bat" "%CONFIG%" "%PLATFORM%" "%DB_PORT%"
if errorlevel 1 exit /b 1

timeout /t 1 /nobreak >nul

call "%ROOT%run_server.bat" "%CONFIG%" "%PLATFORM%" "%SERVER_PORT%" "%DB_HOST%" "%DB_PORT%"
if errorlevel 1 exit /b 1

echo All servers started.
endlocal
exit /b 0
