@echo off
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