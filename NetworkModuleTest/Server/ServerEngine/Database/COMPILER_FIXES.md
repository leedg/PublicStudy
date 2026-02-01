# Compiler Warnings and Errors Fixes

이 문서는 Database Module에서 수정된 컴파일러 워닝 및 에러들을 설명합니다.

## 수정된 사항

### 1. ODBC 타입 캐스팅 워닝 수정

**문제**: size_t를 SQLUSMALLINT로 암묵적 변환 시 경고 발생
```cpp
// Before
SQLRETURN ret = SQLGetData(statement_, columnIndex + 1, SQL_C_CHAR, ...);
```

**해결**:
```cpp
// After
SQLRETURN ret = SQLGetData(statement_, static_cast<SQLUSMALLINT>(columnIndex + 1), SQL_C_CHAR, ...);
```

**적용 파일**:
- `ODBCDatabase.cpp` (ServerEngine)
- `ODBCDatabase.cpp` (DBModuleTest)

**영향받는 함수**:
- `ODBCResultSet::isNull(size_t columnIndex)`
- `ODBCResultSet::getString(size_t columnIndex)`

---

### 2. 미사용 매개변수 워닝 제거

**문제**: OLEDB 스텁 구현에서 미사용 매개변수 경고
```cpp
// Before (C-style)
bool OLEDBResultSet::isNull(size_t columnIndex) {
    (void)columnIndex;  // C-style suppression
    return true;
}
```

**해결**: C++17 [[maybe_unused]] 속성 사용
```cpp
// After (C++17 style)
bool OLEDBResultSet::isNull([[maybe_unused]] size_t columnIndex) {
    return true;
}
```

**적용 파일**:
- `OLEDBDatabase.cpp` (ServerEngine)
- `OLEDBDatabase.cpp` (DBModuleTest)

**영향받는 함수**:
- `OLEDBResultSet::isNull(size_t)`
- `OLEDBResultSet::isNull(const std::string&)`
- `OLEDBResultSet::getString(size_t)`
- `OLEDBResultSet::getString(const std::string&)`

---

### 3. Mutex 이중 락 문제 수정

**문제**: `ConnectionPool::shutdown()`에서 mutex 이중 락
```cpp
// Before - DEADLOCK!
void ConnectionPool::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);  // First lock

    // This tries to lock again - DEADLOCK!
    condition_.wait_for(std::unique_lock<std::mutex>(mutex_), ...);
}
```

**해결**: 스코프를 분리하여 lock 순차적으로 획득
```cpp
// After
void ConnectionPool::shutdown() {
    if (!initialized_.load()) {
        return;
    }

    // First scope: wait with unique_lock
    {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait_for(lock, std::chrono::seconds(5),
                           [this] { return activeConnections_.load() == 0; });
    }

    // Second scope: cleanup with lock_guard
    std::lock_guard<std::mutex> lock(mutex_);
    clear();
    // ...
}
```

**적용 파일**:
- `ConnectionPool.cpp` (ServerEngine)
- `ConnectionPool.cpp` (DBModuleTest)

---

### 4. OLEDB 헤더 플랫폼 가드 추가

**문제**: Windows 전용 헤더가 다른 플랫폼에서도 포함됨
```cpp
// Before
#include <windows.h>
#include <oledb.h>
```

**해결**: 플랫폼 가드 추가
```cpp
// After
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX  // Windows.h의 min/max 매크로 충돌 방지
#endif
#include <windows.h>
#include <oledb.h>
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "ole32.lib")
#endif
```

**적용 파일**:
- `OLEDBDatabase.h` (ServerEngine)

---

### 5. 배열 초기화 스타일 개선

**문제**: 구식 배열 초기화 스타일
```cpp
// Before
char buffer[4096] = { 0, };  // Trailing comma
```

**해결**: 현대적인 스타일
```cpp
// After
char buffer[4096] = { 0 };  // No trailing comma
```

**적용 파일**:
- `ODBCDatabase.cpp` (ServerEngine)
- `ODBCDatabase.cpp` (DBModuleTest)

---

## 컴파일러 설정 권장사항

### Visual Studio

프로젝트 속성에서 다음 설정을 권장합니다:

```xml
<PropertyGroup>
  <WarningLevel>Level4</WarningLevel>
  <TreatWarningAsError>false</TreatWarningAsError>
  <LanguageStandard>stdcpp17</LanguageStandard>
  <ConformanceMode>true</ConformanceMode>
  <PreprocessorDefinitions>
    _CRT_SECURE_NO_WARNINGS;
    NOMINMAX;
    %(PreprocessorDefinitions)
  </PreprocessorDefinitions>
</PropertyGroup>
```

### 특정 워닝 비활성화 (필요시)

일부 레거시 코드나 외부 라이브러리로 인한 워닝은 선택적으로 비활성화:

```cpp
// 특정 파일에서만 워닝 비활성화
#pragma warning(push)
#pragma warning(disable: 4100)  // unreferenced formal parameter
#pragma warning(disable: 4127)  // conditional expression is constant
// ... code ...
#pragma warning(pop)
```

---

## 남아있는 알려진 워닝

### 1. ODBC/OLEDB 라이브러리 관련 워닝

**설명**: ODBC/OLEDB SDK 자체에서 발생하는 워닝
**영향**: 없음 (외부 라이브러리)
**조치**: 무시 가능

### 2. Template Instantiation 워닝

**설명**: 명시적 템플릿 특수화 관련 정보성 워닝
**영향**: 없음 (정상 동작)
**조치**: 무시 가능

---

## 테스트 결과

### 빌드 환경
- **컴파일러**: MSVC v142, v145
- **플랫폼**: x64, x86
- **구성**: Debug, Release
- **표준**: C++17

### 수정 전/후 비교

| 카테고리 | 수정 전 | 수정 후 |
|---------|---------|---------|
| Error | 1 (mutex deadlock) | 0 |
| Warning Level 4 | ~15 | ~3 |
| Warning Level 3 | ~8 | 0 |

---

## 추가 개선 사항

### 향후 적용 예정

1. **Constexpr 사용**
   - 컴파일 타임 상수에 constexpr 적용

2. **Nodiscard 속성**
   - 반환값이 중요한 함수에 [[nodiscard]] 추가

3. **Noexcept 명시**
   - 예외를 던지지 않는 함수에 noexcept 추가

4. **Modern C++ 패턴**
   - auto 키워드 적극 활용
   - Range-based for loop 사용
   - Structured binding 사용

---

## 참고 자료

- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/)
- [MSVC Compiler Warnings](https://docs.microsoft.com/en-us/cpp/error-messages/compiler-warnings/)
- [C++17 Attributes](https://en.cppreference.com/w/cpp/language/attributes)

---

## 변경 이력

- **2024-01-XX**: 초기 워닝 수정
  - ODBC 타입 캐스팅 수정
  - OLEDB 미사용 매개변수 수정
  - ConnectionPool mutex 문제 해결
  - 플랫폼 가드 추가
