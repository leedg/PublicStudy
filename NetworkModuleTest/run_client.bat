@echo off
:: ==============================================================================
:: run_client.bat
:: 역할: TestClient.exe (부하 테스트 클라이언트)를 지정한 수만큼 실행한다.
::       run_client.ps1 의 배치 파일 버전.
::       클라이언트를 1초 간격으로 순차적으로 새 창에서 실행한다.
::
:: 사용법:
::   run_client.bat [Config] [Platform] [TargetHost] [ServerPort] [ClientCount]
::
:: 인자 (순서대로, 모두 선택):
::   %1 Config      : 빌드 구성 (기본값: Debug)
::   %2 Platform    : 빌드 플랫폼 (기본값: x64)
::   %3 TargetHost  : 접속할 서버 주소 (기본값: 127.0.0.1)
::   %4 ServerPort  : 접속할 서버 포트 (기본값: 9000)
::   %5 ClientCount : 실행할 클라이언트 수, 양의 정수 (기본값: 1)
:: ==============================================================================
setlocal enabledelayedexpansion

set "CONFIG=Debug"
set "PLATFORM=x64"
set "TARGET_HOST=127.0.0.1"
set "SERVER_PORT=9000"
set "CLIENT_COUNT=1"

if not "%~1"=="" set "CONFIG=%~1"
if not "%~2"=="" set "PLATFORM=%~2"
if not "%~3"=="" set "TARGET_HOST=%~3"
if not "%~4"=="" set "SERVER_PORT=%~4"
if not "%~5"=="" set "CLIENT_COUNT=%~5"

set "ROOT=%~dp0"
set "BIN=%ROOT%%PLATFORM%\%CONFIG%"
set "CLIENT_EXE=%BIN%\TestClient.exe"

if not exist "%CLIENT_EXE%" (
    echo Executable not found: %CLIENT_EXE%
    exit /b 1
)

echo(%CLIENT_COUNT% | findstr /R "^[1-9][0-9]*$" >nul
if errorlevel 1 (
    echo CLIENT_COUNT must be a positive integer. Current value: %CLIENT_COUNT%
    exit /b 1
)

echo Starting %CLIENT_COUNT% client(s) to %TARGET_HOST%:%SERVER_PORT%
for /L %%I in (1,1,%CLIENT_COUNT%) do (
    echo Starting client %%I
    start "TestClient_%%I" /D "%BIN%" "%CLIENT_EXE%" --host %TARGET_HOST% --port %SERVER_PORT%
    timeout /t 1 /nobreak >nul
)

endlocal
exit /b 0
