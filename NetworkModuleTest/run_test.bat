@echo off
chcp 65001 >nul 2>&1
setlocal enabledelayedexpansion

REM 한글: 기본 설정값
set "CONFIG=Debug"
set "PLATFORM=x64"
set "SERVER_PORT=9000"
set "DB_PORT=8002"
set "HOST=127.0.0.1"

REM 한글: 인자 순서
REM 1: Configuration, 2: Platform, 3: ServerPort, 4: DbPort, 5: Host
if not "%~1"=="" set "CONFIG=%~1"
if not "%~2"=="" set "PLATFORM=%~2"
if not "%~3"=="" set "SERVER_PORT=%~3"
if not "%~4"=="" set "DB_PORT=%~4"
if not "%~5"=="" set "HOST=%~5"

set "ROOT=%~dp0"
set "BIN=%ROOT%%PLATFORM%\%CONFIG%"

set "DBEXE=%BIN%\TestDBServer.exe"
set "SERVEREXE=%BIN%\TestServer.exe"
set "CLIENTEXE=%BIN%\TestClient.exe"

if not exist "%DBEXE%" (
	echo 실행 파일을 찾을 수 없습니다: %DBEXE%
	exit /b 1
)
if not exist "%SERVEREXE%" (
	echo 실행 파일을 찾을 수 없습니다: %SERVEREXE%
	exit /b 1
)
if not exist "%CLIENTEXE%" (
	echo 실행 파일을 찾을 수 없습니다: %CLIENTEXE%
	exit /b 1
)

echo === 테스트 실행 시작 ===
echo Bin: %BIN%

REM 한글: DBServer -> TestServer -> TestClient 순으로 기동
start "" /D "%BIN%" "%DBEXE%" -p %DB_PORT%
timeout /t 1 /nobreak >nul

start "" /D "%BIN%" "%SERVEREXE%" -p %SERVER_PORT%
timeout /t 1 /nobreak >nul

start "" /D "%BIN%" "%CLIENTEXE%" --host %HOST% --port %SERVER_PORT%

echo.
echo 종료하려면 아무 키나 누르세요.
pause >nul

REM 한글: 종료는 클라이언트 -> 서버 -> DBServer 순으로 시도한다.
REM 한글: 동일 이름 프로세스가 여러 개면 모두 종료될 수 있다.
taskkill /IM TestClient.exe /T >nul 2>&1
taskkill /IM TestServer.exe /T >nul 2>&1
taskkill /IM TestDBServer.exe /T >nul 2>&1

echo === 테스트 종료 ===
endlocal
