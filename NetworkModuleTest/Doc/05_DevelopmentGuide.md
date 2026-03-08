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
1. `TestDBServer.exe -p 18002` (Windows 기본, Linux/macOS는 8001)
2. `TestServer.exe -p 19010 --db-host 127.0.0.1 --db-port 18002 -d "<connstr>"` (Windows 기본, Linux/macOS는 9000/8001)
3. `TestClient.exe --host 127.0.0.1 --port 19010` (Windows 기본, Linux/macOS는 9000)
4. 자동 실행: `run_allServer.ps1` 또는 `run_allServer.bat`

> 참고: PowerShell 실행 스크립트(`run_dbServer.ps1` 등)는 기본 포트 충돌 시 자동으로 다음 빈 포트로 fallback 합니다. 필요하면 `-DisablePortFallback`으로 고정할 수 있습니다.

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
- Windows AsyncIO 백엔드 테스트(독립 실행, GTest 불필요):
  - 대상: `IOCPTest`, `RIOTest`
  - 사전 조건: Visual Studio 2022 Build Tools(v143), Windows SDK
  - 빌드:
    - `& "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" ".\Server\Tests\IOCPTest\IOCPTest.vcxproj" /t:Build /p:Configuration=Debug /p:Platform=x64`
    - `& "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" ".\Server\Tests\RIOTest\RIOTest.vcxproj" /t:Build /p:Configuration=Debug /p:Platform=x64`
  - 실행:
    - `.\Server\Tests\IOCPTest\x64\Debug\IOCPTest.exe`
    - `.\Server\Tests\RIOTest\x64\Debug\RIOTest.exe`
  - 성공 기준:
    - `Result: N passed, 0 failed`

## 8. DB 모듈 테스트

### 자동 테스트 (Docker 기반, `scripts/db_tests/`)

각 스크립트가 ODBC 드라이버 설치 → Docker 컨테이너 기동 → 빌드 → 실행 → 정리를 자동으로 수행한다.

```powershell
.\scripts\db_tests\run_sqlite_test.ps1      # SQLite (드라이버 불필요)
.\scripts\db_tests\run_mssql_test.ps1       # MSSQL ODBC (Docker SQL Server)
.\scripts\db_tests\run_postgres_test.ps1    # PostgreSQL ODBC (Docker Postgres)
.\scripts\db_tests\run_mysql_test.ps1       # MySQL ODBC (Docker MySQL)
.\scripts\db_tests\run_oledb_test.ps1       # OLE DB (Docker SQL Server 재사용)
```

테스트 케이스 (`scripts/db_tests/src/db_functional_test.cpp`):

| 테스트 | 내용 | SQLite | ODBC / OLE DB |
|--------|------|:------:|:-------------:|
| T01 | 타입 라운드트립 (string/int/long long/double/bool/null) | ✓ | ✓ |
| T02 | `IsNull()` 후 `GetString()` 동일 컬럼 (컬럼 캐시 검증) | ✓ | ✓ |
| T03 | `FindColumn` 대소문자 무관 | ✓ | ✓ |
| T04 | `Get*()` before `Next()` — 안전한 기본값 반환 | ✓ | — |
| T05 | `IConnection::BeginTransaction` / `Rollback` | ✓ | ✓ |
| T06 | `IDatabase::BeginTransaction` throws (설계 검증) | — | ✓ |

### 수동 테스트 (DBModuleTest, Docker 불필요)

`ModuleTest/DBModuleTest/` 프로젝트를 이용한 대화형/스크립트 기반 로컬 검증.

```powershell
cd ModuleTest/DBModuleTest

# SQLite만 즉시 실행 (드라이버 불필요)
.\scripts\run-db-tests.ps1 -Backend sqlite -Build

# MSSQL ODBC (로컬 SQL Server 필요)
$env:DB_MSSQL_ODBC = "Driver={ODBC Driver 17 for SQL Server};Server=localhost,1433;Database=db_func_test;UID=sa;PWD=Test1234!;TrustServerCertificate=yes;"
.\scripts\run-db-tests.ps1 -Backend mssql

# OLE DB (MSOLEDBSQL 또는 SQLOLEDB, 로컬 SQL Server 필요)
$env:DB_OLEDB = "Provider=MSOLEDBSQL;Server=localhost,1433;Database=db_func_test;UID=sa;PWD=Test1234!;Encrypt=Optional;"
.\scripts\run-db-tests.ps1 -Backend oledb

# 전체 백엔드 순차 실행
.\scripts\run-db-tests.ps1 -Backend all -Build
```

자세한 내용은 `ModuleTest/DBModuleTest/Doc/README.md` 참조.
