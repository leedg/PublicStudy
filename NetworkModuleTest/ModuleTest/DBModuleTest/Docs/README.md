# DBModuleTest — Network::Database 모듈 수동 테스트

`NetworkModuleTest.sln` 내 독립 실행 가능한 DB 모듈 수동 테스트 도구.
`Server/ServerEngine/Database/`의 `Network::Database` 모듈을 직접 참조하여
5개 백엔드를 대화형 메뉴로 선택·테스트한다.

> **자동 테스트(Docker 기반)** 는 `scripts/db_tests/run_*_test.ps1` 사용.
> **수동 테스트(이 프로젝트)** 는 Docker 없이 로컬 드라이버/DB 연결로 동작.

---

## 빠른 시작

### Visual Studio에서 실행

1. `DocDBModule.sln` 열기 (또는 `NetworkModuleTest.sln`에서 DBModuleTest 선택)
2. **x64 / Debug** 선택 후 빌드 (`Ctrl+Shift+B`)
3. 실행 (`Ctrl+F5`) → 대화형 메뉴에서 백엔드 선택

### PowerShell 스크립트로 실행

```powershell
cd ModuleTest/DBModuleTest

# 빌드 후 대화형 메뉴
.\scripts\run-db-tests.ps1 -Build

# SQLite만 (드라이버·Docker 불필요, 즉시 실행)
.\scripts\run-db-tests.ps1 -Backend sqlite -Build

# MSSQL ODBC (드라이버 설치 + 로컬 SQL Server 필요)
$env:DB_MSSQL_ODBC = "Driver={ODBC Driver 17 for SQL Server};Server=localhost,1433;Database=db_func_test;UID=sa;PWD=Test1234!;TrustServerCertificate=yes;"
.\scripts\run-db-tests.ps1 -Backend mssql

# 전체 백엔드 순차 실행
.\scripts\run-db-tests.ps1 -Backend all -Build
```

---

## 지원 백엔드

| # | 백엔드 | 필요 드라이버/설치 |
|---|--------|------------------|
| 1 | SQLite (인메모리) | 없음 (sqlite3 내장) |
| 2 | MSSQL ODBC | ODBC Driver 17/18 for SQL Server |
| 3 | PostgreSQL ODBC | psqlODBC (PostgreSQL Unicode) |
| 4 | MySQL ODBC | MySQL Connector/ODBC 8.x |
| 5 | OLE DB (SQL Server) | SQLOLEDB (Windows 내장) 또는 MSOLEDBSQL |

---

## 테스트 케이스

| 테스트 | 내용 | SQLite | ODBC / OLE DB |
|--------|------|:------:|:-------------:|
| T01 | 타입 라운드트립 (string / int / long long / double / bool / null) | ✓ | ✓ |
| T02 | `IsNull()` 후 `GetString()` 동일 컬럼 (컬럼 캐시 검증) | ✓ | ✓ |
| T03 | `FindColumn` 대소문자 무관 | ✓ | ✓ |
| T04 | `Get*()` before `Next()` — 안전한 기본값 반환 | ✓ | — |
| T05 | `IConnection::BeginTransaction` / `Rollback` | ✓ | ✓ |
| T06 | `IDatabase::BeginTransaction` throws (설계 검증) | — | ✓ |

**SQLite 기준 결과: 23/23 PASS**
**ODBC/OLE DB 기준 결과: 22/22 PASS**

---

## 환경변수 연결 문자열 오버라이드

실행 전 환경변수를 설정하면 기본값 대신 해당 문자열을 사용한다.

| 환경변수 | 대상 |
|----------|------|
| `DB_MSSQL_ODBC` | MSSQL ODBC 연결 문자열 |
| `DB_PGSQL_ODBC` | PostgreSQL ODBC 연결 문자열 |
| `DB_MYSQL_ODBC` | MySQL ODBC 연결 문자열 |
| `DB_OLEDB` | OLE DB 연결 문자열 |

```powershell
# 예시: 포트/비밀번호가 다른 로컬 MySQL
$env:DB_MYSQL_ODBC = "Driver={MySQL ODBC 8.4 Unicode Driver};Server=127.0.0.1;Port=3307;Database=mydb;UID=root;PWD=mypassword;"
.\scripts\run-db-tests.ps1 -Backend mysql
```

---

## 프로젝트 구조

```
ModuleTest/DBModuleTest/
  test_database.cpp             메인 + T01~T06 테스트 (Network::Database PascalCase API)
  DBModuleTest.vcxproj          프로젝트 파일 (Application, x64)
  DocDBModule.sln               독립 솔루션

  sqlite/
    sqlite3.c / sqlite3.h       SQLite 3.45.2 아말감 (내장)

  scripts/
    run-db-tests.ps1            빌드 + 실행 런처

  Docs/
    README.md                   이 파일
    README_SHORT.md             한 줄 요약
    VERSION_SELECTOR.md         VS 버전별 솔루션 선택 가이드

  -- 레거시 파일 (빌드에 포함되지 않음, 참고용) --
  IDatabase.h / DatabaseFactory.h / ODBCDatabase.* / OLEDBDatabase.* / ConnectionPool.*
  odbc_sample.cpp / oledb_sample.cpp
```

> **소스 참조 방식**: `test_database.cpp`가 `../../Server/ServerEngine/Database/*.cpp`를 직접 컴파일한다.
> DBModuleTest 폴더 내 레거시 `.cpp`는 빌드에 포함되지 않는다.

---

## 스크립트 옵션 (`run-db-tests.ps1`)

| 파라미터 | 설명 | 기본값 |
|----------|------|--------|
| `-Backend` | `sqlite` `mssql` `pgsql` `mysql` `oledb` `all` `interactive` | `interactive` |
| `-Config` | `Debug` / `Release` | `Debug` |
| `-ConnStr` | 연결 문자열 직접 지정 (단일 백엔드 시) | — |
| `-Build` | 실행 전 빌드 | false |
| `-Rebuild` | 클린 빌드 | false |

---

## 자동 테스트(Docker)와의 차이

| 항목 | DBModuleTest (이 프로젝트) | `scripts/db_tests/run_*_test.ps1` |
|------|--------------------------|----------------------------------|
| Docker 필요 | ✗ | ✓ |
| 드라이버 자동 설치 | ✗ (수동 설치) | ✓ |
| 실행 방식 | 대화형 메뉴 / PS 스크립트 | 완전 자동 |
| 주 사용 목적 | 개발 중 빠른 로컬 검증 | CI / 회귀 테스트 |
