# DBModuleTest

DBModuleTest is a standalone database module test project.
It uses the `DocDBModule` namespace and lower-case method names.
For new development, use `Server/ServerEngine/Database` instead.

## Layout
```text
ModuleTest/DBModuleTest/
  IDatabase.h
  DatabaseFactory.*
  ODBCDatabase.*
  OLEDBDatabase.*
  ConnectionPool.*
  odbc_sample.cpp
  oledb_sample.cpp
  test_database.cpp
  DBModuleTest.vcxproj
  Doc/
```

## Build
- Preferred: build `DBModuleTest` from `NetworkModuleTest.sln`
- Standalone: open `ModuleTest/DBModuleTest/DBModuleTest.vcxproj`

## Example (DocDBModule API)
```cpp
#include "IDatabase.h"
#include "DatabaseFactory.h"

using namespace DocDBModule;

DatabaseConfig config;
config.type = DatabaseType::ODBC;
config.connectionString = "DSN=MyDatabase;UID=user;PWD=password";

auto db = DatabaseFactory::createDatabase(config.type);
db->connect(config);

auto stmt = db->createStatement();
stmt->setQuery("SELECT * FROM users");
auto rs = stmt->executeQuery();

while (rs->next()) {
    // ...
}
```

## Note
- ServerEngine DB module uses PascalCase API (Connect, CreateStatement, etc).
- This DBModuleTest project remains for legacy/testing purposes.
