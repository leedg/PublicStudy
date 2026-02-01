# Database Module

A comprehensive, cross-platform database access layer for C++ applications with support for multiple database systems.

## Features

- **Abstract Database Interface**: Clean, type-safe interfaces for database operations
- **Multiple Database Support**: ODBC, OLEDB (with extensible architecture for MySQL, PostgreSQL, SQLite)
- **Connection Pooling**: Thread-safe connection pool with automatic resource management
- **RAII Support**: Automatic resource cleanup with scoped connections
- **Prepared Statements**: Safe, efficient parameterized queries
- **Transaction Management**: Full transaction support with commit/rollback
- **Batch Operations**: Execute multiple statements efficiently
- **Exception-based Error Handling**: Clear error reporting with DatabaseException

## Architecture

```
Database Module
├── IDatabase.h            - Core abstract interfaces
├── DatabaseFactory.h/cpp  - Factory for creating database instances
├── ODBCDatabase.h/cpp     - ODBC implementation
├── OLEDBDatabase.h/cpp    - OLEDB implementation
├── ConnectionPool.h/cpp   - Connection pool implementation
├── DatabaseModule.h       - Unified module header
└── Examples/              - Usage examples
    ├── BasicUsage.cpp
    └── ConnectionPoolUsage.cpp
```

## Quick Start

### 1. Basic Database Connection

```cpp
#include "Database/DatabaseModule.h"

using namespace Network::Database;

// Configure database
DatabaseConfig config;
config.type = DatabaseType::ODBC;
config.connectionString = "DSN=MyDatabase;UID=user;PWD=password";

// Create and connect
auto db = DatabaseFactory::createDatabase(config.type);
db->connect(config);

// Execute query
auto stmt = db->createStatement();
stmt->setQuery("SELECT * FROM users");
auto rs = stmt->executeQuery();

while (rs->next()) {
    std::cout << rs->getString("name") << std::endl;
}

db->disconnect();
```

### 2. Using Connection Pool

```cpp
// Configure pool
DatabaseConfig config;
config.type = DatabaseType::ODBC;
config.connectionString = "DSN=MyDatabase;UID=user;PWD=password";
config.maxPoolSize = 10;
config.minPoolSize = 2;

// Initialize pool
ConnectionPool pool;
pool.initialize(config);

// Get connection (manual management)
auto conn = pool.getConnection();
auto stmt = conn->createStatement();
// ... use connection
pool.returnConnection(conn);

// Or use RAII wrapper (recommended)
{
    ScopedConnection scopedConn(pool.getConnection(), &pool);
    auto stmt = scopedConn->createStatement();
    // ... use connection
    // Automatically returned when scope ends
}

pool.shutdown();
```

### 3. Prepared Statements

```cpp
auto stmt = conn->createStatement();
stmt->setQuery("INSERT INTO users (name, age, email) VALUES (?, ?, ?)");

stmt->bindParameter(1, "John Doe");
stmt->bindParameter(2, 30);
stmt->bindParameter(3, "john@example.com");

int rowsAffected = stmt->executeUpdate();
```

### 4. Transactions

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

### 5. Batch Operations

```cpp
auto stmt = conn->createStatement();
stmt->setQuery("INSERT INTO users (name, age) VALUES (?, ?)");

// Add batches
stmt->bindParameter(1, "User1");
stmt->bindParameter(2, 20);
stmt->addBatch();

stmt->clearParameters();
stmt->bindParameter(1, "User2");
stmt->bindParameter(2, 25);
stmt->addBatch();

// Execute all batches
auto results = stmt->executeBatch();
```

## Configuration Options

### DatabaseConfig Structure

```cpp
struct DatabaseConfig {
    std::string connectionString;  // Database connection string
    DatabaseType type;              // ODBC, OLEDB, MySQL, etc.
    int connectionTimeout = 30;     // Connection timeout in seconds
    int commandTimeout = 30;        // Command execution timeout
    bool autoCommit = true;         // Auto-commit mode
    int maxPoolSize = 10;          // Maximum pool connections
    int minPoolSize = 2;           // Minimum pool connections
};
```

### Connection Pool Settings

```cpp
ConnectionPool pool;
pool.setMaxPoolSize(20);           // Maximum connections
pool.setMinPoolSize(5);            // Minimum connections
pool.setConnectionTimeout(30);     // Acquisition timeout (seconds)
pool.setIdleTimeout(300);          // Idle connection timeout (seconds)
```

## Thread Safety

- **ConnectionPool**: Thread-safe for concurrent access
- **IConnection**: NOT thread-safe (use one per thread or protect with mutex)
- **IStatement**: NOT thread-safe (use one per thread)
- **IResultSet**: NOT thread-safe (use one per thread)

**Recommended Pattern**:
```cpp
// Thread function
void workerThread(ConnectionPool& pool) {
    ScopedConnection conn(pool.getConnection(), &pool);
    // Each thread has its own connection
    auto stmt = conn->createStatement();
    // ... use statement
}
```

## Error Handling

All database operations may throw `DatabaseException`:

```cpp
try {
    auto stmt = conn->createStatement();
    stmt->setQuery("SELECT * FROM users");
    auto rs = stmt->executeQuery();
    // ...
}
catch (const DatabaseException& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    std::cerr << "Error code: " << e.getErrorCode() << std::endl;
}
```

## Connection Strings

### ODBC Connection Strings

```cpp
// SQL Server
"Driver={SQL Server};Server=localhost;Database=mydb;UID=user;PWD=pass"

// MySQL
"Driver={MySQL ODBC 8.0 Driver};Server=localhost;Database=mydb;User=user;Password=pass"

// PostgreSQL
"Driver={PostgreSQL Unicode};Server=localhost;Port=5432;Database=mydb;Uid=user;Pwd=pass"

// SQLite
"Driver={SQLite3 ODBC Driver};Database=mydb.db"

// Using DSN
"DSN=MyDataSource;UID=user;PWD=pass"
```

### OLEDB Connection Strings

```cpp
// SQL Server
"Provider=SQLOLEDB;Data Source=localhost;Initial Catalog=mydb;User ID=user;Password=pass"

// Access
"Provider=Microsoft.ACE.OLEDB.12.0;Data Source=C:\\mydb.accdb"
```

## Building

### Prerequisites

- C++17 or later
- Windows: ODBC and OLEDB libraries
- Visual Studio 2019 or later (Windows)
- CMake 3.15+ (optional)

### Integration

1. Include the Database module in your project
2. Link required libraries:
   - Windows: `odbc32.lib`, `oledb.lib`
3. Include the main header:
   ```cpp
   #include "Database/DatabaseModule.h"
   ```

## Examples

Complete examples are available in the `Examples/` directory:

- `BasicUsage.cpp`: Basic database operations
- `ConnectionPoolUsage.cpp`: Connection pool patterns

Build and run examples:
```bash
# Navigate to examples directory
cd ServerEngine/Database/Examples

# Compile (adjust paths as needed)
cl /EHsc /std:c++17 BasicUsage.cpp

# Run
BasicUsage.exe
```

## Best Practices

1. **Use Connection Pooling**: Always use ConnectionPool for production applications
2. **Use ScopedConnection**: Prefer RAII wrapper to prevent connection leaks
3. **Prepared Statements**: Use parameterized queries to prevent SQL injection
4. **Transaction Management**: Use transactions for multi-step operations
5. **Error Handling**: Always catch and handle DatabaseException
6. **Resource Cleanup**: Ensure connections are returned to pool
7. **Thread Safety**: One connection per thread, don't share connections

## Migration from Legacy DBConnection

Legacy `DBConnection` and `DBConnectionPool` classes are deprecated. Migrate to the new module:

### Before (Legacy)
```cpp
DBConnection conn;
conn.Connect("DSN=MyDB;UID=user;PWD=pass");
conn.Execute("SELECT * FROM users");
conn.Disconnect();
```

### After (New Module)
```cpp
DatabaseConfig config;
config.type = DatabaseType::ODBC;
config.connectionString = "DSN=MyDB;UID=user;PWD=pass";
config.maxPoolSize = 10;

ConnectionPool pool;
pool.initialize(config);

ScopedConnection conn(pool.getConnection(), &pool);
auto stmt = conn->createStatement();
stmt->setQuery("SELECT * FROM users");
auto rs = stmt->executeQuery();
```

## Performance Tips

1. **Pool Sizing**: Configure min/max pool size based on workload
2. **Connection Reuse**: Keep connections in pool, don't create/destroy frequently
3. **Batch Operations**: Use batch mode for bulk inserts/updates
4. **Prepared Statements**: Prepare once, execute multiple times
5. **Transactions**: Group related operations into transactions

## Troubleshooting

### "Connection pool timeout"
- Increase `maxPoolSize`
- Check for connection leaks (connections not returned to pool)
- Verify database is accessible

### "Failed to allocate ODBC handle"
- Check ODBC driver installation
- Verify connection string format
- Check database server status

### "SQL Error: [specific error]"
- Check SQL syntax
- Verify table/column names
- Check permissions

## Version Information

- **Version**: 1.0.0
- **Build Date**: See `ModuleVersion::BUILD_DATE`
- **Compatibility**: Windows (ODBC/OLEDB), Linux/macOS (ODBC only)

## License

This module is part of the NetworkModuleTest project.

## Support

For issues and questions:
1. Check the examples in `Examples/` directory
2. Review error messages and codes
3. Consult database-specific documentation

## Future Enhancements

- [ ] MySQL native driver support
- [ ] PostgreSQL native driver support
- [ ] SQLite embedded support
- [ ] Asynchronous database operations
- [ ] Query result caching
- [ ] Connection health monitoring
- [ ] Performance metrics and logging
