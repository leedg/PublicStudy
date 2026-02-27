@echo off
:: ==============================================================================
:: run_server.bat
:: 역할: TestServer.exe (게임 서버)를 새 창으로 실행한다.
::       run_server.ps1 의 배치 파일 버전으로, PowerShell 없이도 사용 가능하다.
::
:: 사용법:
::   run_server.bat [Config] [Platform] [ServerPort] [DbHost] [DbPort]
::
:: 인자 (순서대로, 모두 선택):
::   %1 Config     : 빌드 구성 (기본값: Debug)
::   %2 Platform   : 빌드 플랫폼 (기본값: x64)
::   %3 ServerPort : 클라이언트 수신 포트 (기본값: 9000)
::   %4 DbHost     : DBServer 호스트 (기본값: 127.0.0.1)
::   %5 DbPort     : DBServer 포트 (기본값: 8002)
:: ==============================================================================
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
