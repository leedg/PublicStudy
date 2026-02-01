# Database Module Migration Guide

기존 `DBConnection` 및 `DBConnectionPool`에서 새로운 Database Module로 마이그레이션하는 가이드입니다.

## 개요

새로운 Database Module은 다음과 같은 이점을 제공합니다:
- 추상화된 인터페이스로 다양한 데이터베이스 지원
- 개선된 연결 풀 관리
- 준비된 문(Prepared Statement) 지원
- 트랜잭션 관리
- 배치 작업 지원
- RAII 기반 자동 리소스 관리

## 마이그레이션 단계

### 1단계: 헤더 파일 변경

#### Before (Legacy)
```cpp
#include "Database/DBConnection.h"
#include "Database/DBConnectionPool.h"

using namespace Network::Database;
```

#### After (New Module)
```cpp
#include "Database/DatabaseModule.h"

using namespace Network::Database;
```

### 2단계: 연결 생성 방식 변경

#### Before (Legacy)
```cpp
DBConnection conn;
if (!conn.Connect("DSN=MyDatabase;UID=user;PWD=password")) {
    std::cerr << "Connection failed: " << conn.GetLastError() << std::endl;
    return;
}

// Execute query
if (!conn.Execute("SELECT * FROM users")) {
    std::cerr << "Query failed: " << conn.GetLastError() << std::endl;
}

conn.Disconnect();
```

#### After (New Module)
```cpp
try {
    DatabaseConfig config;
    config.type = DatabaseType::ODBC;
    config.connectionString = "DSN=MyDatabase;UID=user;PWD=password";

    auto db = DatabaseFactory::createDatabase(config.type);
    db->connect(config);

    auto stmt = db->createStatement();
    stmt->setQuery("SELECT * FROM users");
    auto rs = stmt->executeQuery();

    while (rs->next()) {
        // Process results
    }

    db->disconnect();
}
catch (const DatabaseException& e) {
    std::cerr << "Error: " << e.what() << std::endl;
}
```

### 3단계: 연결 풀 마이그레이션

#### Before (Legacy)
```cpp
DBConnectionPool& pool = DBConnectionPool::Instance();
pool.Initialize("DSN=MyDatabase;UID=user;PWD=password", 10);

// Acquire connection
DBConnectionRef conn = pool.Acquire();
conn->Execute("SELECT * FROM users");

// Release connection
pool.Release(conn);

pool.Shutdown();
```

#### After (New Module)
```cpp
DatabaseConfig config;
config.type = DatabaseType::ODBC;
config.connectionString = "DSN=MyDatabase;UID=user;PWD=password";
config.maxPoolSize = 10;
config.minPoolSize = 2;

ConnectionPool pool;
pool.initialize(config);

// Method 1: Manual management
auto conn = pool.getConnection();
auto stmt = conn->createStatement();
stmt->setQuery("SELECT * FROM users");
auto rs = stmt->executeQuery();
pool.returnConnection(conn);

// Method 2: RAII (Recommended)
{
    ScopedConnection scopedConn(pool.getConnection(), &pool);
    auto stmt = scopedConn->createStatement();
    stmt->setQuery("SELECT * FROM users");
    auto rs = stmt->executeQuery();
    // Connection automatically returned
}

pool.shutdown();
```

### 4단계: 준비된 문 사용 (신기능)

New Module은 준비된 문을 지원합니다:

```cpp
auto stmt = conn->createStatement();
stmt->setQuery("SELECT * FROM users WHERE age > ? AND name LIKE ?");

// Bind parameters
stmt->bindParameter(1, 25);
stmt->bindParameter(2, "John%");

auto rs = stmt->executeQuery();
while (rs->next()) {
    std::string name = rs->getString("name");
    int age = rs->getInt("age");
    std::cout << "Name: " << name << ", Age: " << age << std::endl;
}
```

### 5단계: 트랜잭션 사용 (신기능)

```cpp
auto conn = pool.getConnection();

try {
    conn->beginTransaction();

    // Execute multiple operations
    auto stmt1 = conn->createStatement();
    stmt1->setQuery("UPDATE accounts SET balance = balance - ? WHERE id = ?");
    stmt1->bindParameter(1, 100.0);
    stmt1->bindParameter(2, 1);
    stmt1->executeUpdate();

    auto stmt2 = conn->createStatement();
    stmt2->setQuery("UPDATE accounts SET balance = balance + ? WHERE id = ?");
    stmt2->bindParameter(1, 100.0);
    stmt2->bindParameter(2, 2);
    stmt2->executeUpdate();

    conn->commitTransaction();
}
catch (const DatabaseException& e) {
    conn->rollbackTransaction();
    std::cerr << "Transaction failed: " << e.what() << std::endl;
}

pool.returnConnection(conn);
```

## 주요 변경사항

### API 변경

| Legacy | New Module | 비고 |
|--------|------------|------|
| `DBConnection::Connect()` | `IDatabase::connect()` | 예외 기반 에러 처리 |
| `DBConnection::Disconnect()` | `IDatabase::disconnect()` | - |
| `DBConnection::Execute()` | `IStatement::executeQuery()` / `executeUpdate()` | 분리된 메서드 |
| `DBConnection::IsConnected()` | `IDatabase::isConnected()` | - |
| `DBConnection::GetLastError()` | `DatabaseException::what()` | 예외로 변경 |
| `DBConnectionPool::Acquire()` | `ConnectionPool::getConnection()` | - |
| `DBConnectionPool::Release()` | `ConnectionPool::returnConnection()` | - |
| N/A | `ScopedConnection` | RAII 래퍼 (신규) |

### 에러 처리 변경

#### Before (Legacy)
```cpp
DBConnection conn;
if (!conn.Connect("...")) {
    std::cerr << conn.GetLastError() << std::endl;
}
```

#### After (New Module)
```cpp
try {
    auto db = DatabaseFactory::createDatabase(DatabaseType::ODBC);
    db->connect(config);
}
catch (const DatabaseException& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    std::cerr << "Code: " << e.getErrorCode() << std::endl;
}
```

### 리소스 관리 개선

#### Before (Legacy)
```cpp
DBConnectionRef conn = pool.Acquire();
// ... use connection
// Must remember to release
pool.Release(conn);
```

#### After (New Module)
```cpp
{
    ScopedConnection conn(pool.getConnection(), &pool);
    // ... use connection
    // Automatically released when scope ends
}
```

## 호환성 모드

기존 코드를 즉시 마이그레이션할 수 없는 경우, 두 방식을 병행 사용할 수 있습니다:

```cpp
// Legacy code still works
#include "Database/DBConnection.h"
DBConnection legacyConn;
legacyConn.Connect("...");

// New code
#include "Database/DatabaseModule.h"
ConnectionPool newPool;
newPool.initialize(config);
```

**주의**: 레거시 코드는 향후 제거될 예정이므로, 가능한 빨리 마이그레이션하는 것을 권장합니다.

## 성능 개선

새로운 Database Module은 다음과 같은 성능 개선을 제공합니다:

1. **연결 풀 최적화**: 더 효율적인 연결 관리 및 재사용
2. **준비된 문**: 반복적인 쿼리 성능 향상
3. **배치 작업**: 다량의 INSERT/UPDATE 성능 개선
4. **리소스 자동 관리**: 메모리 누수 방지

## 마이그레이션 체크리스트

- [ ] 모든 `DBConnection` 사용을 `IDatabase` 또는 `ConnectionPool`로 변경
- [ ] 에러 처리를 `if (!conn.Connect())` 방식에서 `try-catch` 방식으로 변경
- [ ] 리소스 해제를 수동 방식에서 RAII 방식(`ScopedConnection`)으로 변경
- [ ] 준비된 문을 사용하도록 쿼리 수정 (SQL Injection 방지)
- [ ] 관련 트랜잭션이 있는 경우 트랜잭션 API 사용
- [ ] 배치 작업이 필요한 경우 배치 API 사용
- [ ] 테스트 코드 작성 및 실행
- [ ] 성능 벤치마크 수행

## 추가 리소스

- [Database Module README](README.md)
- [Basic Usage Examples](Examples/BasicUsage.cpp)
- [Connection Pool Examples](Examples/ConnectionPoolUsage.cpp)
- [API Documentation](IDatabase.h)

## 문제 해결

### "DatabaseException: Database not connected"
- `connect()` 호출 후에 작업을 수행하세요
- 연결 풀이 초기화되었는지 확인하세요

### "Connection pool timeout"
- `maxPoolSize` 증가
- 연결 누수 확인 (반환되지 않은 연결)
- `ScopedConnection` 사용 권장

### "Prepared statement parameter binding failed"
- 파라미터 인덱스는 1부터 시작합니다 (0이 아님)
- 파라미터 타입이 올바른지 확인하세요

## 지원

마이그레이션 중 문제가 발생하면:
1. 예제 코드를 참조하세요
2. 에러 메시지와 코드를 확인하세요
3. 레거시 코드와 새 코드를 비교하세요
