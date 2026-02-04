# Development Guide

## Environment
- Windows 10+ (primary)
- Visual Studio 2022, C++17
- Windows SDK, ODBC/OLEDB
- Protobuf/GTest optional

## Build
### Visual Studio (recommended)
1. Open `NetworkModuleTest.sln`
2. Select `x64` Debug/Release
3. Build solution

### CMake
- Root CMake only builds `ModuleTest/MultiPlatformNetwork`
- `Server/ServerEngine` CMake may be out of sync with current layout

## Run
1. `TestDBServer.exe -p 8002`
2. `TestServer.exe -p 9000 -d "<connstr>"` (optional)
3. `TestClient.exe --host 127.0.0.1 --port 9000`
4. Auto run: `run_test.ps1` or `run_test.bat`

## Tests
- `Server/ServerEngine/Tests/` contains sources but no default target
- Enable GTest and add targets as needed
