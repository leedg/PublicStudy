@echo off
:: ==============================================================================
:: run_allServer.bat
:: 역할: TestDBServer 와 TestServer 를 올바른 순서로 한 번에 실행한다.
::       run_dbServer.bat → 1초 대기 → run_server.bat 순으로 호출한다.
::       run_allServer.ps1 의 배치 파일 버전.
::
:: 사용법:
::   run_allServer.bat [Config] [Platform] [ServerPort] [DbPort] [DbHost]
::
:: 인자 (순서대로, 모두 선택):
::   %1 Config     : 빌드 구성 (기본값: Debug)
::   %2 Platform   : 빌드 플랫폼 (기본값: x64)
::   %3 ServerPort : TestServer 수신 포트 (기본값: 9000)
::   %4 DbPort     : TestDBServer 수신 포트 (기본값: 8002)
::   %5 DbHost     : TestServer 가 접속할 DBServer 주소 (기본값: 127.0.0.1)
:: ==============================================================================
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
