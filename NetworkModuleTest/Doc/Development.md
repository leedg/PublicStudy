# 개발 가이드

## 개발 환경
- Windows 10+ (기본)
- Visual Studio 2022, C++17
- Windows SDK, ODBC/OLEDB
- Protobuf/GTest 옵션

## 빌드
### Visual Studio (권장)
1. `NetworkModuleTest.sln` 열기
2. `x64` Debug/Release 선택
3. 빌드

### CMake
- 루트 CMake는 `ModuleTest/MultiPlatformNetwork`만 빌드
- `Server/ServerEngine` CMake는 현재 구조와 일부 불일치 가능

## 실행
1. `TestDBServer.exe -p 8002`
2. `TestServer.exe -p 9000 -d "<connstr>"` (옵션)
3. `TestClient.exe --host 127.0.0.1 --port 9000`
4. 자동 실행: `run_test.ps1` 또는 `run_test.bat`

## 테스트
- `Server/ServerEngine/Tests/`에 테스트 소스 존재
- 기본 빌드 타깃에는 포함되지 않으므로 필요 시 GTest 연동 추가
