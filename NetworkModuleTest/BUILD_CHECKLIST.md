# Database Module Build Checklist

## 빌드 전 체크리스트

### 1. 필수 요구사항
- [ ] Visual Studio 2019 이상 설치
- [ ] Windows SDK 10.0 설치
- [ ] C++17 표준 지원
- [ ] ODBC 드라이버 설치 (테스트용)

### 2. 프로젝트 파일 확인
- [x] `DBModuleTest.vcxproj` 업데이트 완료
- [x] `ServerEngine.vcxproj` 업데이트 완료
- [x] 모든 소스 파일 추가됨
- [x] 모든 헤더 파일 추가됨

### 3. 코드 수정 사항
- [x] ODBC 타입 캐스팅 수정
- [x] OLEDB 미사용 매개변수 처리
- [x] ConnectionPool mutex 이중 락 수정
- [x] 플랫폼 가드 추가
- [x] 배열 초기화 스타일 개선

## 빌드 단계

### DBModuleTest 빌드

```bash
# Visual Studio 개발자 명령 프롬프트에서 실행

# 1. 디렉토리 이동
cd C:\MyGithub\PublicStudy\NetworkModuleTest\ModuleTest\DBModuleTest

# 2. Debug x64 빌드
msbuild DBModuleTest.vcxproj /p:Configuration=Debug /p:Platform=x64

# 3. Release x64 빌드
msbuild DBModuleTest.vcxproj /p:Configuration=Release /p:Platform=x64
```

**예상 결과**:
- Error: 0
- Warning (Level 4): 0-3개 (외부 라이브러리 관련)

### ServerEngine 빌드

```bash
# 1. 디렉토리 이동
cd C:\MyGithub\PublicStudy\NetworkModuleTest\Server\ServerEngine

# 2. Debug x64 빌드
msbuild ServerEngine.vcxproj /p:Configuration=Debug /p:Platform=x64

# 3. Release x64 빌드
msbuild ServerEngine.vcxproj /p:Configuration=Release /p:Platform=x64
```

**예상 결과**:
- Error: 0
- Warning (Level 4): 0-5개 (외부 라이브러리 및 기존 코드 관련)

## 빌드 후 검증

### 1. 출력 파일 확인

**DBModuleTest**:
```
x64/Debug/DBModuleTest.lib
x64/Release/DBModuleTest.lib
```

**ServerEngine**:
```
x64/Debug/ServerEngine.lib
x64/Release/ServerEngine.lib
```

### 2. 링크 테스트

예제 프로그램으로 링크 테스트:

```cpp
// test_link.cpp
#include "Database/DatabaseModule.h"
#include <iostream>

int main() {
    using namespace Network::Database;

    std::cout << "Database Module Version: "
              << ModuleVersion::VERSION_STRING << std::endl;

    return 0;
}
```

```bash
# 컴파일 및 링크
cl /EHsc /std:c++17 /I"C:\MyGithub\PublicStudy\NetworkModuleTest\Server\ServerEngine" test_link.cpp /link ServerEngine.lib odbc32.lib

# 실행
test_link.exe
```

**예상 출력**:
```
Database Module Version: 1.0.0
```

## 일반적인 빌드 문제 해결

### 문제 1: LNK2019 - Unresolved external symbol

**원인**: 라이브러리 링크 누락

**해결**:
```cpp
#pragma comment(lib, "odbc32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "ole32.lib")
```

또는 프로젝트 속성:
```
Linker > Input > Additional Dependencies
odbc32.lib;oleaut32.lib;ole32.lib
```

### 문제 2: C2872 - 'byte': ambiguous symbol

**원인**: Windows.h와 std::byte 충돌

**해결**:
```cpp
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define _WINSOCKAPI_  // Prevent winsock.h inclusion
#include <windows.h>
```

### 문제 3: C4251 - class needs to have dll-interface

**원인**: DLL 경계를 넘는 STL 클래스

**해결**:
- Static Library로 빌드 (현재 구성)
- 또는 워닝 비활성화:
```cpp
#pragma warning(disable: 4251)
```

### 문제 4: C2039 - 'make_unique': is not a member of 'std'

**원인**: C++14/17 표준 미설정

**해결**:
프로젝트 속성에서:
```
C/C++ > Language > C++ Language Standard > ISO C++17 Standard
```

## 경고 레벨별 예상 결과

### Level 3 (/W3)
- Error: 0
- Warning: 0-2

### Level 4 (/W4) - 권장
- Error: 0
- Warning: 0-5 (대부분 외부 라이브러리)

### Wall (/Wall) - 참고용
- Warning: 10-20 (STL 및 Windows SDK 포함)
- 프로덕션 빌드에는 비권장

## CI/CD 통합 스크립트

```batch
@echo off
echo Building Database Module...

REM DBModuleTest
cd C:\MyGithub\PublicStudy\NetworkModuleTest\ModuleTest\DBModuleTest
msbuild DBModuleTest.vcxproj /p:Configuration=Release /p:Platform=x64 /v:minimal
if %ERRORLEVEL% NEQ 0 (
    echo DBModuleTest build failed!
    exit /b 1
)

REM ServerEngine
cd C:\MyGithub\PublicStudy\NetworkModuleTest\Server\ServerEngine
msbuild ServerEngine.vcxproj /p:Configuration=Release /p:Platform=x64 /v:minimal
if %ERRORLEVEL% NEQ 0 (
    echo ServerEngine build failed!
    exit /b 1
)

echo All builds successful!
exit /b 0
```

## 성능 빌드 옵션

### Release 최적화 설정

```xml
<PropertyGroup Condition="'$(Configuration)'=='Release'">
  <WholeProgramOptimization>true</WholeProgramOptimization>
  <FunctionLevelLinking>true</FunctionLevelLinking>
  <IntrinsicFunctions>true</IntrinsicFunctions>
  <EnableCOMDATFolding>true</EnableCOMDATFolding>
  <OptimizeReferences>true</OptimizeReferences>
  <Optimization>MaxSpeed</Optimization>
  <InlineFunctionExpansion>AnySuitable</InlineFunctionExpansion>
</PropertyGroup>
```

## 최종 확인

### 빌드 성공 기준
- [x] DBModuleTest Debug 빌드 성공
- [x] DBModuleTest Release 빌드 성공
- [x] ServerEngine Debug 빌드 성공
- [x] ServerEngine Release 빌드 성공
- [x] Error 0개
- [x] Warning Level 4에서 5개 이하
- [ ] 링크 테스트 성공
- [ ] 예제 프로그램 실행 성공

## 다음 단계

빌드가 성공하면:
1. [ ] 단위 테스트 실행
2. [ ] 통합 테스트 실행
3. [ ] 메모리 누수 검사 (Valgrind/Dr. Memory)
4. [ ] 성능 벤치마크
5. [ ] 문서 최종 검토

---

**마지막 업데이트**: 2024-01-XX
**빌드 환경**: Visual Studio 2019/2022, Windows 10/11, MSVC v142/v145
