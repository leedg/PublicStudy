@echo off
REM Database Module Build Script
REM Builds both DBModuleTest and ServerEngine Database modules

setlocal enabledelayedexpansion

echo ============================================
echo Database Module Build Script
echo ============================================
echo.

REM Check if running in Visual Studio Developer Command Prompt
where msbuild >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: MSBuild not found in PATH
    echo Please run this script from Visual Studio Developer Command Prompt
    echo Or run: "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
    exit /b 1
)

REM Configuration
set BUILD_CONFIG=Release
set BUILD_PLATFORM=x64
set VERBOSITY=minimal

REM Parse arguments
:parse_args
if "%~1"=="" goto end_parse
if /i "%~1"=="debug" set BUILD_CONFIG=Debug
if /i "%~1"=="release" set BUILD_CONFIG=Release
if /i "%~1"=="x86" set BUILD_PLATFORM=x86
if /i "%~1"=="x64" set BUILD_PLATFORM=x64
if /i "%~1"=="verbose" set VERBOSITY=normal
shift
goto parse_args
:end_parse

echo Configuration: %BUILD_CONFIG%
echo Platform: %BUILD_PLATFORM%
echo.

REM Save current directory
set SCRIPT_DIR=%~dp0
cd /d "%SCRIPT_DIR%"

REM Build DBModuleTest
echo [1/2] Building DBModuleTest...
echo ----------------------------------------
cd ModuleTest\DBModuleTest
if not exist DBModuleTest.vcxproj (
    echo ERROR: DBModuleTest.vcxproj not found
    exit /b 1
)

msbuild DBModuleTest.vcxproj /p:Configuration=%BUILD_CONFIG% /p:Platform=%BUILD_PLATFORM% /v:%VERBOSITY% /nologo
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ERROR: DBModuleTest build failed!
    cd /d "%SCRIPT_DIR%"
    exit /b 1
)

echo.
echo DBModuleTest build successful!
echo.

REM Build ServerEngine
echo [2/2] Building ServerEngine...
echo ----------------------------------------
cd "%SCRIPT_DIR%"
cd Server\ServerEngine
if not exist ServerEngine.vcxproj (
    echo ERROR: ServerEngine.vcxproj not found
    exit /b 1
)

msbuild ServerEngine.vcxproj /p:Configuration=%BUILD_CONFIG% /p:Platform=%BUILD_PLATFORM% /v:%VERBOSITY% /nologo
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ERROR: ServerEngine build failed!
    cd /d "%SCRIPT_DIR%"
    exit /b 1
)

echo.
echo ServerEngine build successful!
echo.

REM Return to original directory
cd /d "%SCRIPT_DIR%"

REM Summary
echo ============================================
echo Build Summary
echo ============================================
echo Configuration: %BUILD_CONFIG%
echo Platform: %BUILD_PLATFORM%
echo.

REM Check output files
set DBMODULE_LIB=ModuleTest\DBModuleTest\%BUILD_PLATFORM%\%BUILD_CONFIG%\DBModuleTest.lib
set SERVERENGINE_LIB=Server\ServerEngine\%BUILD_PLATFORM%\%BUILD_CONFIG%\ServerEngine.lib

if exist "%DBMODULE_LIB%" (
    echo [OK] DBModuleTest.lib
) else (
    echo [WARN] DBModuleTest.lib not found at expected location
)

if exist "%SERVERENGINE_LIB%" (
    echo [OK] ServerEngine.lib
) else (
    echo [WARN] ServerEngine.lib not found at expected location
)

echo.
echo ============================================
echo All builds completed successfully!
echo ============================================

exit /b 0
