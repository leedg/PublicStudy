@echo off
:: ==============================================================================
:: build.bat  (ModuleTest/DBModuleTest/Doc/build.bat)
:: 역할: DBModuleTest 빌드에 필요한 출력 디렉토리를 미리 생성하고,
::       Visual Studio 에서 직접 빌드하는 방법을 안내한다.
::
:: 동작:
::   - bin\Win32\Debug, bin\Win32\Release, bin\x64\Debug, bin\x64\Release,
::     obj 디렉토리를 생성한다 (없는 경우에만).
::   - 실제 컴파일은 Visual Studio 또는 MSBuild 로 수행해야 한다.
::
:: 사용법:
::   build.bat
::   (인자 없음 — 디렉토리 초기화 용도)
:: ==============================================================================
echo Building DocDBModule...

REM Create build directories
if not exist "bin" mkdir bin
if not exist "bin\Win32" mkdir bin\Win32
if not exist "bin\Win32\Debug" mkdir bin\Win32\Debug
if not exist "bin\Win32\Release" mkdir bin\Win32\Release
if not exist "bin\x64" mkdir bin\x64
if not exist "bin\x64\Debug" mkdir bin\x64\Debug
if not exist "bin\x64\Release" mkdir bin\x64\Release

if not exist "obj" mkdir obj

echo Build directories created successfully!
echo.
echo To build in Visual Studio:
echo 1. Open DocDBModule.sln in Visual Studio 2019 or later
echo 2. Select desired configuration (Debug/Release) and platform (Win32/x64)
echo 3. Build Solution (Ctrl+Shift+B)
echo.
echo Projects available:
echo - DocDBModule: Static library
echo - ODBC Sample: ODBC usage example
echo - OLEDB Sample: OLEDB usage example  
echo - DB Tests: Unit tests
echo.
pause