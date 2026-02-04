# Development Guide (Detailed)

## 1. Prerequisites
- Visual Studio 2022, C++17
- Windows SDK
- ODBC/OLEDB drivers
- Protobuf/GTest optional

## 2. Solution build
1. Open `NetworkModuleTest.sln`
2. Select `x64` Debug/Release
3. Recommended order: ServerEngine -> TestDBServer -> TestServer -> TestClient

## 3. Run order
1. `TestDBServer.exe -p 8002`
2. `TestServer.exe -p 9000 -d "<connstr>"` (optional)
3. `TestClient.exe --host 127.0.0.1 --port 9000`
4. Auto run: `run_test.ps1` or `run_test.bat`

## 4. CMake
- Root CMake builds only `ModuleTest/MultiPlatformNetwork`
- Other CMake files are reference and may not match current tree

## 5. Logs
- Logger levels: DEBUG/INFO/WARN/ERROR
- TestServer/TestClient support `-l` option

## 6. Tests
- AsyncIOProvider tests require GTest integration
