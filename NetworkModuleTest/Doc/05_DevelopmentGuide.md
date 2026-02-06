# 개발 가이드 (상세)

## 1. 준비 사항
- Visual Studio 2022, C++17
- Windows SDK
- ODBC/OLEDB 드라이버
- Protobuf/GTest 옵션

## 2. 솔루션 빌드
1. `NetworkModuleTest.sln` 열기
2. `x64` Debug/Release 선택
3. 권장 빌드 순서: ServerEngine -> TestDBServer -> TestServer -> TestClient

## 3. 실행 순서
1. `TestDBServer.exe -p 8002`
2. `TestServer.exe -p 9000 -d "<connstr>"` (옵션)
3. `TestClient.exe --host 127.0.0.1 --port 9000`
4. 자동 실행: `run_test.ps1` 또는 `run_test.bat`

## 4. CMake 사용
- 루트 CMake는 `ModuleTest/MultiPlatformNetwork`만 빌드
- 다른 CMake는 참고용이며 소스 구조와 불일치 가능

## 5. DB 사용
- TestServer 프로젝트에 `ENABLE_DATABASE_SUPPORT` 전처리 정의 추가

## 6. 로그/디버깅
- Logger 레벨: DEBUG/INFO/WARN/ERROR
- TestServer/TestClient는 `-l` 옵션 제공

## 7. 테스트
- AsyncIOProvider 테스트는 GTest 연동 시 사용 가능
