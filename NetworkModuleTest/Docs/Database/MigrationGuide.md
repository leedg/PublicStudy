# Database 모듈 마이그레이션 가이드

레거시 DBConnection/DBConnectionPool에서 DatabaseModule로 전환하는 방법입니다.

## 1. 헤더 변경
### Before
```cpp
#include "Database/DBConnection.h"
#include "Database/DBConnectionPool.h"
```

### After
```cpp
#include "Database/DatabaseModule.h"
```

## 2. 연결 방식 변경
### Before
```cpp
DBConnection conn;
conn.Connect("DSN=MyDatabase;UID=user;PWD=password");
conn.Execute("SELECT * FROM users");
conn.Disconnect();
```

### After
```cpp
DatabaseConfig config;
config.type = DatabaseType::ODBC;
config.connectionString = "DSN=MyDatabase;UID=user;PWD=password";

auto db = DatabaseFactory::CreateDatabase(config.type);
db->Connect(config);

auto stmt = db->CreateStatement();
stmt->SetQuery("SELECT * FROM users");
auto rs = stmt->ExecuteQuery();

db->Disconnect();
```

## 3. ConnectionPool 변경
### Before
```cpp
DBConnectionPool& pool = DBConnectionPool::Instance();
pool.Initialize("DSN=MyDatabase;UID=user;PWD=password", 10);

auto conn = pool.Acquire();
conn->Execute("SELECT * FROM users");

pool.Release(conn);
pool.Shutdown();
```

### After
```cpp
DatabaseConfig config;
config.type = DatabaseType::ODBC;
config.connectionString = "DSN=MyDatabase;UID=user;PWD=password";
config.maxPoolSize = 10;
config.minPoolSize = 2;

ConnectionPool pool;
pool.Initialize(config);

auto conn = pool.GetConnection();
auto stmt = conn->CreateStatement();
stmt->SetQuery("SELECT * FROM users");
auto rs = stmt->ExecuteQuery();

pool.ReturnConnection(conn);
pool.Shutdown();
```

## 4. RAII 사용
```cpp
{
    ScopedConnection scoped(pool.GetConnection(), &pool);
    auto stmt = scoped->CreateStatement();
    stmt->SetQuery("SELECT * FROM users");
}
```
