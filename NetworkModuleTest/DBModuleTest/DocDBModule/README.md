# DocDBModule

**DBModuleTest 프로젝트에 대한 문서화된 정보입니다.**

> **⚠️ 중요**: 실제 코드 구현은 [`../DBModuleTest/`](../DBModuleTest/) 폴더에 있습니다. 여기에는 문서와 독립 실행 솔루션 파일만 있습니다.

## 📁 최종 구조

```
DocDBModule/                          # 📖 문서화된 정보만
├── README.md                        # 상세 문서 (원본)
├── README_SHORT.md                   # 간단 문서
├── VERSION_SELECTOR.md               # VS 버전 선택 가이드
├── DocDBModule.sln                 # 독립 실행 솔루션 (VS 2022)
├── DocDBModule_vs2015.sln           # VS 2015 호환
├── DocDBModule_vs2010.sln           # VS 2010 호환
└── build.bat                        # 빌드 스크립트

../DBModuleTest/                      # 🚀 실제 코드 구현
├── include/                          # 헤더 파일
│   └── IDatabase.h                  # 핵심 인터페이스
├── src/                             # 소스 코드
│   ├── DatabaseFactory.cpp           # 팩토리 구현
│   └── odbc/                        # ODBC 구현
│       ├── ODBCDatabase.h
│       └── ODBCDatabase.cpp
├── samples/                         # 샘플 코드
│   ├── odbc_sample.cpp              # ODBC 예제
│   └── oledb_sample.cpp             # OLEDB 예제
├── tests/                           # 테스트 코드
│   └── test_database.cpp            # 단위 테스트
├── DBModuleTest.vcxproj            # VS 프로젝트 파일
├── DBModuleTest.vcxproj.filters     # 필터 파일
└── CMakeLists.txt                  # CMake 파일

../NetworkModuleTest.sln              # 🏗️ 메인 솔루션 (DBModuleTest 포함)
```

## 🎯 주요 기능

### **데이터베이스 추상화**
- ✅ **ODBC**와 **OLEDB** 통합 인터페이스
- ✅ **RAII** 기반 자동 자원 관리
- ✅ **타입 세이프** 파라미터 바인딩
- ✅ **커넥션 풀링** 지원
- ✅ **예외 기반** 에러 처리

### **현대 C++ 설계**
- ✅ **C++17** 표준 준수
- ✅ **스마트 포인터** 기반 메모리 관리
- ✅ **템플릿 메타프로그래밍** 활용
- ✅ **인터페이스 분리** 설계 패턴

## 🚀 사용 방법

### **1. 메인 솔루션에서 개발 (권장)**
```bash
# 메인 솔루션 열기
start ../NetworkModuleTest.sln
# DBModuleTest 프로젝트 포함됨
```

### **2. 독립 실행**
```bash
# 독립 솔루션 열기
start DocDBModule.sln
# DBModuleTest 프로젝트 참조
```

### **3. 빌드**
```bash
# 간단 빌드
build.bat

# 또는 Visual Studio에서 직접 빌드
# Build → Build Solution (Ctrl+Shift+B)
```

## 💻 코드 예제

```cpp
#include "IDatabase.h"
using namespace DocDBModule;

// ODBC 데이터베이스 생성
auto database = DatabaseFactory::createODBCDatabase();

// 연결 설정
DatabaseConfig config;
config.connectionString = "DRIVER={SQL Server};SERVER=localhost;DATABASE=TestDB;Trusted_Connection=Yes;";
config.type = DatabaseType::ODBC;

// 연결 및 쿼리 실행
database->connect(config);
auto statement = database->createStatement();
statement->setQuery("SELECT * FROM users WHERE age > ?");
statement->bindParameter(1, 25);
auto resultSet = statement->executeQuery();

// 결과 처리
while (resultSet->next()) {
    std::cout << "Name: " << resultSet->getString("name") 
              << ", Age: " << resultSet->getInt("age") << std::endl;
}
```

## 📖 상세 문서

- [전체 기능 문서](README.md)
- [VS 버전 선택 가이드](VERSION_SELECTOR.md)
- [API 레퍼런스](../DBModuleTest/include/IDatabase.h)
- [CMake 빌드 가이드](../DBModuleTest/CMakeLists.txt)

## 🔗 관련 프로젝트

- **[DBModuleTest](../DBModuleTest/)** - 실제 코드 구현
- **[NetworkModuleTest.sln](../NetworkModuleTest.sln)** - 메인 솔루션

## ⚙️ 지원 환경

### **Visual Studio 버전**
- ✅ **Visual Studio 2022** (권장)
- ✅ **Visual Studio 2019** 
- ✅ **Visual Studio 2017**
- ✅ **Visual Studio 2015**
- ✅ **Visual Studio 2010**

### **플랫폼**
- ✅ **Windows x64** (권장)
- ✅ **Windows x86** (Win32)

### **데이터베이스**
- ✅ **SQL Server** (ODBC/OLEDB)
- ✅ **MySQL** (ODBC)
- ✅ **PostgreSQL** (ODBC)
- ✅ **Oracle** (ODBC/OLEDB)
- ✅ **SQLite** (ODBC)

## 🎉 시작하기

1. **솔루션 열기**: `../NetworkModuleTest.sln` 또는 `DocDBModule.sln`
2. **DBModuleTest 선택**: 프로젝트 탐색기에서 DBModuleTest 선택
3. **빌드**: Ctrl+Shift+B 로 빌드
4. **실행**: F5로 디버그 실행
5. **코드 탐색**: DBModuleTest 폴더에서 실제 구현 확인

---

**💡 팁**: 가장 좋은 개발 경험을 위해 **Visual Studio 2022**에서 **../NetworkModuleTest.sln** 열기를 권장합니다.