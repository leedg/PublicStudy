@echo off
:: ==============================================================================
:: run_dbServer.bat
:: 역할: TestDBServer.exe (DB 연동 서버)를 새 창으로 실행한다.
::       run_dbServer.ps1 의 배치 파일 버전.
::       게임 서버(TestServer) 보다 먼저 실행해야 한다.
::
:: 사용법:
::   run_dbServer.bat [Config] [Platform] [DbPort]
::
:: 인자 (순서대로, 모두 선택):
::   %1 Config   : 빌드 구성 (기본값: Debug)
::   %2 Platform : 빌드 플랫폼 (기본값: x64)
::   %3 DbPort   : 수신 포트 (기본값: 8002)
:: ==============================================================================
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
