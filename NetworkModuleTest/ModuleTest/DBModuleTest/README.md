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

## DB 테스트 가이드

`DBModuleTest.vcxproj`는 정적 라이브러리만 빌드합니다.
실행 파일(`db_tests`, `odbc_sample`, `oledb_sample`)은 CMake 타깃으로 빌드해야 합니다.

### 1) 사전 점검

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\check-db-prereqs.ps1
```

DB endpoint까지 필수로 확인하려면:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\check-db-prereqs.ps1 -RequireDatabase
```

### 2) 테스트 실행

기본 실행 (`db_tests`만 실행):

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run-db-tests.ps1
```

샘플까지 실행 (`odbc_sample`, `oledb_sample` 포함):

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run-db-tests.ps1 -RunSamples
```

DB 접속 실패를 실패로 강제하려면:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run-db-tests.ps1 -RequireDatabase
```

### 3) 연결 문자열 지정 (선택)

환경변수 또는 실행 인자로 전달할 수 있습니다.

- `DOCDB_ODBC_CONN`
- `DOCDB_OLEDB_CONN`
- `DOCDB_REQUIRE_DB` (`1`이면 DB 접속 실패를 테스트 실패로 간주)

예시:

```powershell
$env:DOCDB_ODBC_CONN = 'DRIVER={ODBC Driver 18 for SQL Server};SERVER=localhost;DATABASE=master;Trusted_Connection=Yes;Encrypt=no'
$env:DOCDB_OLEDB_CONN = 'Provider=MSOLEDBSQL;Server=localhost;Database=master;Integrated Security=SSPI;Encrypt=Optional'
powershell -ExecutionPolicy Bypass -File .\scripts\run-db-tests.ps1 -RunSamples -RequireDatabase
```
