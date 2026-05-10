# Database 모듈 (ServerEngine)

`Server/ServerEngine/Database/` — `Network::Database` 네임스페이스의 통합 DB 접근 레이어.
5개 백엔드 지원, 모두 동일한 PascalCase 인터페이스 사용.

---

## 지원 백엔드

| 백엔드 | 클래스 | 플랫폼 | 테스트 결과 |
|--------|--------|--------|-------------|
| SQLite (인메모리/파일) | `SQLiteDatabase` | Win/Linux/macOS | 23/23 PASS |
| ODBC — SQL Server | `ODBCDatabase` | Windows | 22/22 PASS |
| ODBC — PostgreSQL | `ODBCDatabase` | Windows | 22/22 PASS |
| ODBC — MySQL | `ODBCDatabase` | Windows | 22/22 PASS |
| OLE DB — SQL Server | `OLEDBDatabase` | Windows only | 22/22 PASS |
| Mock (단위 테스트용) | `MockDatabase` | 모든 플랫폼 | — |

---

## 디렉터리 구조

```
Server/ServerEngine/Database/
  DatabaseFactory.h/.cpp     팩토리 (CreateODBCDatabase / CreateOLEDBDatabase / CreateSQLiteDatabase)
  DatabaseModule.h            통합 헤더
  ODBCDatabase.h/.cpp         ODBC 구현 (SQL Server / PostgreSQL / MySQL 공통)
  OLEDBDatabase.h/.cpp        OLE DB 구현 (SQLOLEDB / MSOLEDBSQL 공급자)
  SQLiteDatabase.h/.cpp       SQLite 구현 (HAVE_SQLITE3 정의 필요)
  MockDatabase.h/.cpp         단위 테스트용 Mock
  ConnectionPool.h/.cpp       연결 풀

Interfaces/
  IDatabase.h / IConnection.h / IStatement.h / IResultSet.h
  DatabaseConfig.h            연결 설정 구조체
  DatabaseException.h         예외 클래스
  DatabaseType_enum.h         ODBC / OLEDB / SQLite / Mock
```

---

## 기본 사용 패턴

```cpp
#include "Database/DatabaseModule.h"
using namespace Network::Database;

// 1. DB 생성
auto db = DatabaseFactory::CreateODBCDatabase();   // 또는 CreateOLEDBDatabase(), CreateSQLiteDatabase()

// 2. 연결
DatabaseConfig cfg;
cfg.mConnectionString = "Driver={ODBC Driver 17 for SQL Server};Server=...;";
db->Connect(cfg);

// 3. 쿼리
auto stmt = db->CreateStatement();
stmt->SetQuery("SELECT id, name FROM users WHERE id = ?");
stmt->BindParameter(1, 42);
auto rs = stmt->ExecuteQuery();

while (rs->Next()) {
    int id       = rs->GetInt("id");
    std::string name = rs->GetString("name");
}

// 4. 연결 해제
db->Disconnect();
```

---

## 트랜잭션

IDatabase 수준 트랜잭션은 ODBC/OLE DB 백엔드에서 **지원하지 않음** (throws).
트랜잭션이 필요하면 `IConnection`을 사용한다.

```cpp
auto conn = db->CreateConnection();
conn->Open(cfg.mConnectionString);

conn->BeginTransaction();
try {
    auto stmt = conn->CreateStatement();
    stmt->SetQuery("INSERT INTO orders VALUES (?, ?)");
    stmt->BindParameter(1, orderId);
    stmt->BindParameter(2, amount);
    stmt->ExecuteUpdate();
    conn->CommitTransaction();
} catch (...) {
    conn->RollbackTransaction();
    throw;
}
```

---

## 팩토리 선택 기준

```cpp
// ODBC — 범용, 드라이버 설치 필요
auto db = DatabaseFactory::CreateODBCDatabase();

// OLE DB — SQL Server 전용, COM 기반, Windows only
auto db = DatabaseFactory::CreateOLEDBDatabase();

// SQLite — 내장 DB, 외부 의존 없음 (HAVE_SQLITE3 + sqlite3.c 링크)
auto db = DatabaseFactory::CreateSQLiteDatabase();

// 런타임 선택
auto db = DatabaseFactory::CreateDatabase(DatabaseType::ODBC);
```

---

## 연결 문자열 예시

```cpp
// SQL Server ODBC
"Driver={ODBC Driver 17 for SQL Server};Server=localhost,1433;"
"Database=mydb;UID=sa;PWD=password;TrustServerCertificate=yes;"

// PostgreSQL ODBC
"Driver={PostgreSQL Unicode};Server=localhost;Port=5432;"
"Database=mydb;UID=postgres;PWD=password;"

// MySQL ODBC
"Driver={MySQL ODBC 8.4 Unicode Driver};Server=127.0.0.1;Port=3306;"
"Database=mydb;UID=root;PWD=password;"

// OLE DB (SQL Server, SQLOLEDB)
"Provider=SQLOLEDB;Data Source=localhost,1433;"
"Initial Catalog=mydb;User Id=sa;Password=password;TrustServerCertificate=yes;"

// OLE DB (SQL Server, MSOLEDBSQL — 최신 드라이버)
"Provider=MSOLEDBSQL;Server=localhost,1433;"
"Database=mydb;UID=sa;PWD=password;Encrypt=Optional;"

// SQLite
":memory:"              // 인메모리
"C:/data/mydb.sqlite"  // 파일
```

---

## 주요 버그 수정 이력

| 날짜 | 항목 |
|------|------|
| 2026-03-08 | OLE DB 완전 구현 (`IDataInitialize` → `IAccessor` 전체 COM 파이프라인) |
| 2026-03-08 | OLE DB 버퍼 정렬 버그 수정 (`DBLENGTH` = ULONGLONG 8바이트 → `kLengthOff=8`, `kValueOff=16`) |
| 2026-03-08 | `GetSQLErrorMessage` 미초기화 버퍼 수정 (`= {}` 초기화) |
| 2026-03-08 | PostgreSQL bool 바인딩 `SQL_C_BIT/SQL_BIT` 으로 수정 |
| 2026-03-08 | `ODBCStatement`의 dangling handle 수정 (`mOwnerConn` 패턴) |

---

## 테스트 방법

### 1. 자동 테스트 스크립트 (Docker 필요)

```powershell
# 각 백엔드별 독립 실행
.\scripts\db_tests\run_sqlite_test.ps1
.\scripts\db_tests\run_mssql_test.ps1
.\scripts\db_tests\run_postgres_test.ps1
.\scripts\db_tests\run_mysql_test.ps1
.\scripts\db_tests\run_oledb_test.ps1
```

각 스크립트는 필요한 ODBC 드라이버를 자동 설치하고 Docker 컨테이너를 시작·정리한다.

### 2. DBModuleTest 수동 테스트 (Docker 불필요)

```powershell
# Visual Studio 에서 열기
ModuleTest/DBModuleTest/DocDBModule.sln

# 또는 스크립트로 빌드 + 실행
cd ModuleTest/DBModuleTest
.\scripts\run-db-tests.ps1 -Build          # 빌드 후 대화형 메뉴
.\scripts\run-db-tests.ps1 -Backend sqlite  # SQLite 즉시 실행
```

자세한 내용은 `ModuleTest/DBModuleTest/Docs/README.md` 참조.

---

## Linux 빌드 주의

ODBCDatabase / OLEDBDatabase는 `#ifdef _WIN32` 로 가드되어 있다.
Linux/macOS에서는 SQLite 또는 MockDatabase만 사용 가능.
