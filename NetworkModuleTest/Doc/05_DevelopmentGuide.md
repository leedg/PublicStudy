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
1. `TestDBServer.exe -p 8001` (기본)
2. `TestServer.exe -p 9000 --db-host 127.0.0.1 --db-port 8001 -d "<connstr>"` (옵션)
3. `TestClient.exe --host 127.0.0.1 --port 9000`
4. 자동 실행: `run_test.ps1` 또는 `run_test.bat`

> 참고: `run_test.ps1` 기본값은 DB 포트를 `8002`로 전달합니다.

## 4. CMake 사용
- 루트 CMake는 `ModuleTest/MultiPlatformNetwork`만 빌드
- 다른 CMake는 참고용이며 소스 구조와 불일치 가능

## 5. DB 사용
- TestServer 프로젝트에 `ENABLE_DATABASE_SUPPORT` 전처리 정의 추가

## 6. 로그/디버깅
- Logger 레벨: DEBUG/INFO/WARN/ERROR
- TestServer/TestDBServer/TestClient는 `-l` 옵션 제공

## 7. 테스트
- 구조 동기화 정적 검증:
  - `.\ModuleTest\ServerStructureSync\validate_server_structure_sync.ps1`
  - 검증 항목: DBTaskQueue 워커 정책, weak_ptr 주입, Stop() 종료 순서, 재연결 정책, Wiki 초안 반영
- 자동 통합 테스트:
  - `.\run_test_auto.ps1 -RunSeconds 5`
  - 기본 동작: 실행 전 서버 구조 동기화 검증을 먼저 수행
  - 동기화 검증 생략: `.\run_test_auto.ps1 -RunSeconds 5 -SkipStructureSyncCheck`
- AsyncIOProvider 테스트는 GTest 연동 시 사용 가능
