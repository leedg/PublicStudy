# 솔루션 가이드

## 솔루션 프로젝트
`NetworkModuleTest.sln`에 포함된 주요 프로젝트:

| 프로젝트 | vcxproj 위치 | 유형 | 설명 |
|----------|-------------|------|------|
| ServerEngine | `Server/ServerEngine/ServerEngine.vcxproj` | StaticLibrary | 핵심 엔진 (Network, Database, …) |
| TestDBServer | `Server/DBServer/TestDBServer.vcxproj` | Application | DB 서버 (SQLite 세션 저장) |
| TestServer | `Server/TestServer/TestServer.vcxproj` | Application | 에코/패킷 처리 서버 |
| TestClient | `Client/TestClient/TestClient.vcxproj` | Application | 부하/연결 테스트 클라이언트 |
| DBModuleTest | `ModuleTest/DBModuleTest/DBModuleTest.vcxproj` | **Application** | Network::Database 수동 테스트 도구 |
| MultiPlatformNetwork | `ModuleTest/MultiPlatformNetwork/MultiPlatformNetwork.vcxproj` | Application | 플랫폼별 네트워크 비교 |

솔루션 폴더: `1.Thirdparty`, `2.Lib`, `3.Server`, `8.Client`, `9.Test`, `ModuleTest`, `Documentation`

## 빌드 구성
- Debug / Release
- x64 (x86은 DBModuleTest 미지원 — OLE DB COM 라이브러리 x64 전용)

## 권장 빌드 순서
1. ServerEngine
2. TestDBServer
3. TestServer
4. TestClient
5. DBModuleTest, MultiPlatformNetwork (선택)

## DBModuleTest 특이사항

`DBModuleTest`는 **`ServerEngine.lib` 참조 없이** `ServerEngine/Database/*.cpp` 소스를 직접 컴파일하는 독립 실행형 테스트 도구다.

- **독립 솔루션**: `ModuleTest/DBModuleTest/DocDBModule.sln`으로도 열 수 있음 (VS 버전 선택 가이드: `ModuleTest/DBModuleTest/Docs/VERSION_SELECTOR.md`)
- **SQLite 내장**: `sqlite/sqlite3.c` (아말감 v3.45.2) — 외부 드라이버 불필요
- **ODBC/OLE DB**: 링크 라이브러리 `odbc32.lib`, `odbccp32.lib`, `ole32.lib`, `oleaut32.lib`, `msdasc.lib`
- **5개 백엔드 지원**: SQLite / MSSQL ODBC / PostgreSQL ODBC / MySQL ODBC / OLE DB (SQL Server)

빠른 실행:
```powershell
cd ModuleTest/DBModuleTest
.\scripts\run-db-tests.ps1 -Backend sqlite -Build   # 즉시 실행 (드라이버 불필요)
.\scripts\run-db-tests.ps1 -Backend all             # 전체 백엔드 (드라이버/DB 필요)
```

자세한 내용은 `ModuleTest/DBModuleTest/Docs/README.md` 참조.

## CMake 현황
- 루트 CMake는 MultiPlatformNetwork만 빌드
- Linux Docker 통합 테스트: `test_linux/CMakeLists.txt` (ServerEngine + TestServer + TestClient)
- 다른 CMake는 참고용
