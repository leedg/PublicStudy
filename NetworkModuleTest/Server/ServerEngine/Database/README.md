# Database 모듈 (ServerEngine)

ODBC/OLEDB 구현을 포함한 통합 DB 접근 레이어입니다.
PascalCase API와 RAII 헬퍼를 사용합니다.

## 주요 기능
- 추상 인터페이스: IDatabase, IConnection, IStatement, IResultSet
- ConnectionPool 기반 연결 재사용
- ODBC/OLEDB 구현
- RAII 지원(ScopedConnection)
- 예외 기반 오류 처리(DatabaseException)

## 구조
```text
Database/
  DatabaseModule.h
  DatabaseFactory.*
  ConnectionPool.*
  ODBCDatabase.*
  OLEDBDatabase.*
  DBConnection.*        (legacy)
  DBConnectionPool.*    (legacy)
  Examples/
```

## 기본 사용
```cpp
#include "Database/DatabaseModule.h"

using namespace Network::Database;

DatabaseConfig config;
config.type = DatabaseType::ODBC;
config.connectionString = "DSN=MyDatabase;UID=user;PWD=password";

ConnectionPool pool;
pool.Initialize(config);

auto conn = pool.GetConnection();
auto stmt = conn->CreateStatement();
stmt->SetQuery("SELECT * FROM users");
auto rs = stmt->ExecuteQuery();

pool.ReturnConnection(conn);
```

## RAII 예시
```cpp
ScopedConnection scoped(pool.GetConnection(), &pool);
auto stmt = scoped->CreateStatement();
stmt->SetQuery("SELECT * FROM users");
```

## 참고
- DBConnection/DBConnectionPool은 레거시 API입니다.
- 신규 코드는 DatabaseModule.h 사용 권장
