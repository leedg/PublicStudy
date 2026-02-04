# Database Module (ServerEngine)

A unified database access layer with ODBC/OLEDB implementations.
Uses PascalCase API and RAII helpers.

## Features
- Abstract interfaces: IDatabase, IConnection, IStatement, IResultSet
- ConnectionPool for reuse
- ODBC and OLEDB implementations
- RAII support (ScopedConnection)
- Exception-based error handling (DatabaseException)

## Structure
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

## Basic usage
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

## RAII example
```cpp
ScopedConnection scoped(pool.GetConnection(), &pool);
auto stmt = scoped->CreateStatement();
stmt->SetQuery("SELECT * FROM users");
```

## Notes
- DBConnection/DBConnectionPool are legacy APIs.
- New code should use DatabaseModule.h.
