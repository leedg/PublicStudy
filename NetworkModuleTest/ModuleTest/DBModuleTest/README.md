# DBModuleTest - Database Module Testing Framework

이 프로젝트는 데이터베이스 모듈의 독립적인 테스트 및 개발 환경입니다.

## 개요

DBModuleTest는 다음을 제공합니다:
- ODBC 및 OLEDB 데이터베이스 추상화 레이어
- 연결 풀 (Connection Pool) 구현
- 준비된 문 (Prepared Statement) 지원
- 트랜잭션 관리
- 배치 작업 지원

## 프로젝트 구조

```
DBModuleTest/
├── IDatabase.h            - 핵심 추상 인터페이스
├── DatabaseFactory.h/cpp  - 데이터베이스 팩토리
├── ODBCDatabase.h/cpp     - ODBC 구현
├── OLEDBDatabase.h/cpp    - OLEDB 구현
├── ConnectionPool.h/cpp   - 연결 풀 구현
├── test_database.cpp      - 테스트 코드
└── Doc/                   - 문서
    ├── README.md
    └── VERSION_SELECTOR.md
```

## 특징

### 1. 추상화된 데이터베이스 인터페이스
```cpp
class IDatabase {
    virtual void connect(const DatabaseConfig& config) = 0;
    virtual void disconnect() = 0;
    virtual std::unique_ptr<IConnection> createConnection() = 0;
    virtual std::unique_ptr<IStatement> createStatement() = 0;
    // ...
};
```

### 2. 연결 풀 (Connection Pool)
```cpp
ConnectionPool pool;
DatabaseConfig config;
config.type = DatabaseType::ODBC;
config.connectionString = "DSN=MyDB;...";
config.maxPoolSize = 10;

pool.initialize(config);
auto conn = pool.getConnection();
// ... use connection
pool.returnConnection(conn);
```

### 3. RAII 기반 연결 관리
```cpp
{
    ScopedConnection conn(pool.getConnection(), &pool);
    auto stmt = conn->createStatement();
    // ... use statement
    // 자동으로 연결이 풀로 반환됨
}
```

### 4. 준비된 문 (Prepared Statement)
```cpp
auto stmt = conn->createStatement();
stmt->setQuery("SELECT * FROM users WHERE age > ?");
stmt->bindParameter(1, 25);
auto rs = stmt->executeQuery();
```

### 5. 트랜잭션 지원
```cpp
conn->beginTransaction();
try {
    // ... execute operations
    conn->commitTransaction();
} catch (...) {
    conn->rollbackTransaction();
}
```

## 빌드 방법

### Visual Studio
1. `DBModuleTest.sln` 열기
2. 빌드 구성 선택 (Debug/Release)
3. 빌드 실행

### CMake
```bash
mkdir build
cd build
cmake ..
cmake --build .
```

## 사용 예제

### 기본 쿼리 실행
```cpp
#include "IDatabase.h"
#include "DatabaseFactory.h"

using namespace DocDBModule;

DatabaseConfig config;
config.type = DatabaseType::ODBC;
config.connectionString = "DSN=MyDatabase;UID=user;PWD=pass";

auto db = DatabaseFactory::createDatabase(config.type);
db->connect(config);

auto stmt = db->createStatement();
stmt->setQuery("SELECT * FROM users");
auto rs = stmt->executeQuery();

while (rs->next()) {
    std::cout << rs->getString("name") << std::endl;
}
```

### 연결 풀 사용
```cpp
ConnectionPool pool;
pool.initialize(config);

// 방법 1: 수동 관리
auto conn = pool.getConnection();
auto stmt = conn->createStatement();
// ... use statement
pool.returnConnection(conn);

// 방법 2: RAII (권장)
{
    ScopedConnection conn(pool.getConnection(), &pool);
    auto stmt = conn->createStatement();
    // ... use statement
}
```

## 테스트

테스트 파일들:
- `test_database.cpp` - 통합 테스트
- `odbc_sample.cpp` - ODBC 샘플
- `oledb_sample.cpp` - OLEDB 샘플

테스트 실행:
```bash
# 빌드 후
./DBModuleTest.exe
```

## ServerEngine 통합

이 모듈은 `ServerEngine/Database`에 통합되어 있습니다:

```
NetworkModuleTest/
├── ModuleTest/DBModuleTest/     (이 프로젝트 - 독립 테스트)
└── Server/ServerEngine/Database/ (통합 모듈)
```

ServerEngine에서 사용:
```cpp
#include "Database/DatabaseModule.h"

using namespace Network::Database;

// 동일한 API 사용
DatabaseConfig config;
config.type = DatabaseType::ODBC;
// ...
```

## 의존성

- C++17 이상
- Windows SDK
- ODBC 드라이버
- OLEDB 프로바이더 (선택사항)

## 연결 문자열 예제

### ODBC
```cpp
// SQL Server
"Driver={SQL Server};Server=localhost;Database=mydb;UID=user;PWD=pass"

// MySQL
"Driver={MySQL ODBC 8.0 Driver};Server=localhost;Database=mydb;User=user;Password=pass"

// PostgreSQL
"Driver={PostgreSQL Unicode};Server=localhost;Database=mydb;Uid=user;Pwd=pass"

// DSN 사용
"DSN=MyDataSource;UID=user;PWD=pass"
```

### OLEDB
```cpp
// SQL Server
"Provider=SQLOLEDB;Data Source=localhost;Initial Catalog=mydb;User ID=user;Password=pass"

// Access
"Provider=Microsoft.ACE.OLEDB.12.0;Data Source=C:\\mydb.accdb"
```

## API 문서

### DatabaseConfig
```cpp
struct DatabaseConfig {
    std::string connectionString;  // 연결 문자열
    DatabaseType type;              // 데이터베이스 타입
    int connectionTimeout = 30;     // 연결 타임아웃
    int commandTimeout = 30;        // 명령 타임아웃
    bool autoCommit = true;         // 자동 커밋
    int maxPoolSize = 10;          // 최대 풀 크기
    int minPoolSize = 2;           // 최소 풀 크기
};
```

### IConnection
```cpp
virtual void open(const std::string& connectionString) = 0;
virtual void close() = 0;
virtual bool isOpen() const = 0;
virtual std::unique_ptr<IStatement> createStatement() = 0;
virtual void beginTransaction() = 0;
virtual void commitTransaction() = 0;
virtual void rollbackTransaction() = 0;
```

### IStatement
```cpp
virtual void setQuery(const std::string& query) = 0;
virtual void bindParameter(size_t index, const T& value) = 0;
virtual std::unique_ptr<IResultSet> executeQuery() = 0;
virtual int executeUpdate() = 0;
virtual void addBatch() = 0;
virtual std::vector<int> executeBatch() = 0;
```

### IResultSet
```cpp
virtual bool next() = 0;
virtual std::string getString(const std::string& columnName) = 0;
virtual int getInt(const std::string& columnName) = 0;
virtual long long getLong(const std::string& columnName) = 0;
virtual double getDouble(const std::string& columnName) = 0;
virtual bool getBool(const std::string& columnName) = 0;
virtual bool isNull(const std::string& columnName) = 0;
```

## 성능 최적화 팁

1. **연결 풀 사용**: 항상 연결 풀을 사용하여 연결 생성 오버헤드 감소
2. **준비된 문 재사용**: 동일한 쿼리는 한 번만 준비하고 여러 번 실행
3. **배치 작업**: 다량의 INSERT/UPDATE는 배치로 처리
4. **트랜잭션**: 관련된 여러 작업은 트랜잭션으로 그룹화
5. **연결 반환**: 사용 후 즉시 연결을 풀로 반환

## 스레드 안전성

- **ConnectionPool**: 스레드 안전
- **IConnection**: 스레드 안전하지 않음 (스레드당 하나씩 사용)
- **IStatement**: 스레드 안전하지 않음
- **IResultSet**: 스레드 안전하지 않음

## 에러 처리

모든 데이터베이스 작업은 `DatabaseException`을 발생시킬 수 있습니다:

```cpp
try {
    auto stmt = conn->createStatement();
    stmt->setQuery("SELECT * FROM users");
    auto rs = stmt->executeQuery();
}
catch (const DatabaseException& e) {
    std::cerr << "오류: " << e.what() << std::endl;
    std::cerr << "에러 코드: " << e.getErrorCode() << std::endl;
}
```

## 버전

- **1.0.0**: 초기 릴리스
  - ODBC 및 OLEDB 지원
  - 연결 풀 구현
  - 준비된 문 지원
  - 트랜잭션 관리

## 라이선스

NetworkModuleTest 프로젝트의 일부입니다.

## 참고

- ServerEngine 통합 모듈 문서: `../../Server/ServerEngine/Database/README.md`
- ODBC 프로그래밍 가이드: https://docs.microsoft.com/en-us/sql/odbc/
- OLEDB 프로그래밍 가이드: https://docs.microsoft.com/en-us/sql/oledb/
