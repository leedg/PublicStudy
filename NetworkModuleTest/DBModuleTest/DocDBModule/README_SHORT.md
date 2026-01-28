# DocDBModule

DocDBModule의 문서화된 정보입니다. 실제 코드 구현은 `../DBModuleTest/` 폴더에 있습니다.

## 📁 구조

```
DocDBModule/
├── README.md                    # 상세 문서
├── DocDBModule.sln             # 독립 솔루션 파일
├── VERSION_SELECTOR.md          # VS 버전 선택 가이드
└── build.bat                   # 빌드 스크립트

DBModuleTest/                  # 실제 코드 위치
├── include/                    # 헤더 파일
├── src/                       # 소스 코드
├── samples/                   # 샘플 코드
├── tests/                     # 테스트 코드
└── DBModuleTest.vcxproj       # VS 프로젝트 파일
```

## 🎯 주요 기능

- **ODBC/OLEDB 통합 인터페이스**
- **RAII 기반 자원 관리**
- **타입 세이프 파라미터 바인딩**
- **커넥션 풀링**
- **예외 기반 에러 처리**

## 📖 사용 방법

### 1. Visual Studio에서 열기
```bash
# 메인 솔루션 열기 (권장)
start ../NetworkModuleTest.sln

# 또는 독립 솔루션 열기
start DocDBModule.sln
```

### 2. 코드 위치
- **실제 소스 코드**: `../DBModuleTest/`
- **샘플 프로그램**: `../DBModuleTest/samples/`
- **테스트 코드**: `../DBModuleTest/tests/`

### 3. 빌드 방법
```bash
# 솔루션에서 DBModuleTest 프로젝트 빌드
# 또는
build.bat
```

## 🚀 빠른 시작

```cpp
#include "IDatabase.h"
using namespace DocDBModule;

auto database = DatabaseFactory::createODBCDatabase();
DatabaseConfig config;
config.connectionString = "DRIVER={SQL Server};SERVER=localhost;DATABASE=TestDB;Trusted_Connection=Yes;";
config.type = DatabaseType::ODBC;

database->connect(config);
auto statement = database->createStatement();
statement->setQuery("SELECT * FROM users");
auto resultSet = statement->executeQuery();

while (resultSet->next()) {
    std::cout << "Name: " << resultSet->getString("name") << std::endl;
}
```

## 📚 상세 문서

- [전체 문서](README.md)
- [VS 버전 선택 가이드](VERSION_SELECTOR.md)
- [API 레퍼런스](../DBModuleTest/include/IDatabase.h)

## 🔗 관련 프로젝트

- [DBModuleTest](../DBModuleTest/) - 실제 구현 코드
- [NetworkModuleTest](../NetworkModuleTest.sln) - 메인 솔루션