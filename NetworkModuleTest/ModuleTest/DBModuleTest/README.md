# DBModuleTest

DBModuleTest는 독립 DB 모듈 테스트 프로젝트입니다.
DocDBModule 네임스페이스와 소문자 API를 사용합니다.
신규 개발은 `Server/ServerEngine/Database` 모듈 사용을 권장합니다.

## 구성
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

## 빌드
- 권장: `NetworkModuleTest.sln`에서 `DBModuleTest` 빌드
- 단독: `ModuleTest/DBModuleTest/DBModuleTest.vcxproj` 열기

## 예시 (DocDBModule API)
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

## 참고
- ServerEngine DB 모듈은 PascalCase API 사용 (Connect, CreateStatement 등)
- DBModuleTest는 레거시/테스트 용도로 유지
