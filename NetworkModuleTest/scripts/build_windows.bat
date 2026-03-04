@echo off
REM 한글: Windows용 빌드 래퍼 스크립트
REM 한글: PowerShell 스크립트를 호출한다.

set SCRIPT_DIR=%~dp0
set PS1=%SCRIPT_DIR%build_windows.ps1

powershell -NoProfile -ExecutionPolicy Bypass -File "%PS1%" %*
exit /b %ERRORLEVEL%
