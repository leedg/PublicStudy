# CMake Build Configuration Guide

**작성일**: 2026-01-27  
**버전**: 1.0  
**대상**: NetworkModule 크로스 플랫폼 빌드 설정  
**목표**: Windows/Linux/macOS 통일 빌드 시스템 구성

---

## 📋 목차

1. [개요](#개요)
2. [CMake 기본 구조](#cmake-기본-구조)
3. [루트 CMakeLists.txt](#루트-cmakelists-txt)
4. [플랫폼별 조건부 컴파일](#플랫폼별-조건부-컴파일)
5. [의존성 관리](#의존성-관리)
6. [빌드 옵션 설정](#빌드-옵션-설정)
7. [테스트 및 벤치마크](#테스트-및-벤치마크)
8. [배포 및 설치](#배포-및-설치)
9. [문제 해결](#문제-해결)

---

## 개요

### 목적

CMake 빌드 시스템을 통해 다음을 달성합니다:

- ✅ **크로스 플랫폼 통일**: Windows/Linux/macOS에서 동일한 명령어 사용
- ✅ **자동 플랫폼 감지**: OS별 최적의 I/O 백엔드 선택
- ✅ **유연한 옵션**: 사용자가 빌드 옵션 커스터마이징 가능
- ✅ **테스트 통합**: GTest 기반 자동 테스트
- ✅ **패키지 배포**: CPack을 이용한 설치 가능한 바이너리

### 핵심 구성

```
NetworkModule/
├── CMakeLists.txt                    (루트 CMakeLists)
├── cmake/                            (CMake 헬퍼 모듈)
│   ├── PlatformDetection.cmake       (플랫폼 감지)
│   ├── CompilerSettings.cmake        (컴파일러 설정)
│   └── Dependencies.cmake            (의존성 관리)
├── src/                              (소스 코드)
│   ├── CMakeLists.txt
│   ├── AsyncIO/
│   │   ├── CMakeLists.txt
│   │   ├── Common/
│   │   ├── Platform/
│   │   │   ├── Windows/
│   │   │   ├── Linux/
│   │   │   └── macOS/
│   │   └── ...
│   └── ...
├── test/                             (테스트)
│   ├── CMakeLists.txt
│   └── ...
├── benchmark/                        (벤치마크)
│   ├── CMakeLists.txt
│   └── ...
└── docs/                             (문서)
```

---

## CMake 기본 구조

### 최소 CMake 버전 요구

```cmake
cmake_minimum_required(VERSION 3.15)
project(NetworkModule
    VERSION 1.0.0
    DESCRIPTION "Cross-platform network I/O library"
    LANGUAGES CXX
)

# C++ 표준
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 출력 디렉토리
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
```

### 좋은 practice

```cmake
# 빌드 타입별 기본값
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# 옵션 선언
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(NETWORK_MODULE_ENABLE_TESTING "Enable unit tests" ON)
option(NETWORK_MODULE_ENABLE_BENCHMARKS "Enable benchmarks" ON)
option(NETWORK_MODULE_ENABLE_RIO "Enable Windows RIO backend" ON)
option(NETWORK_MODULE_ENABLE_IOURING "Enable Linux io_uring backend" ON)

# 메시지 출력
message(STATUS "NetworkModule Build Configuration")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  CMake version: ${CMAKE_VERSION}")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID}")
```

---

## 루트 CMakeLists.txt

### 완전한 루트 CMakeLists.txt 예제

```cmake
cmake_minimum_required(VERSION 3.15)

project(NetworkModule
    VERSION 1.0.0
    DESCRIPTION "Cross-platform network I/O library"
    LANGUAGES CXX C
)

# =============================================================================
# 전역 설정
# =============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# =============================================================================
# CMake 모듈 경로
# =============================================================================

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# =============================================================================
# 빌드 옵션
# =============================================================================

option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(NETWORK_MODULE_ENABLE_TESTING "Enable unit tests" ON)
option(NETWORK_MODULE_ENABLE_BENCHMARKS "Enable benchmarks" ON)
option(NETWORK_MODULE_ENABLE_RIO "Enable Windows RIO backend" ON)
option(NETWORK_MODULE_ENABLE_IOURING "Enable Linux io_uring backend" ON)
option(NETWORK_MODULE_ENABLE_KQUEUE "Enable macOS kqueue backend" ON)
option(NETWORK_MODULE_ENABLE_EPOLL "Enable Linux epoll backend" ON)
option(NETWORK_MODULE_ENABLE_EXAMPLES "Build example applications" ON)

# =============================================================================
# 플랫폼 감지
# =============================================================================

include(PlatformDetection)

# =============================================================================
# 컴파일러 설정
# =============================================================================

include(CompilerSettings)

# =============================================================================
# 의존성 관리
# =============================================================================

include(Dependencies)

# =============================================================================
# 메인 라이브러리
# =============================================================================

add_subdirectory(src)

# =============================================================================
# 테스트
# =============================================================================

if(NETWORK_MODULE_ENABLE_TESTING)
    enable_testing()
    add_subdirectory(test)
endif()

# =============================================================================
# 벤치마크
# =============================================================================

if(NETWORK_MODULE_ENABLE_BENCHMARKS)
    add_subdirectory(benchmark)
endif()

# =============================================================================
# 예제 프로그램
# =============================================================================

if(NETWORK_MODULE_ENABLE_EXAMPLES)
    add_subdirectory(examples EXCLUDE_FROM_ALL)
endif()

# =============================================================================
# 설치 설정
# =============================================================================

install(
    DIRECTORY "include/"
    DESTINATION "include"
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

install(
    TARGETS NetworkModule
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

# =============================================================================
# 빌드 정보 출력
# =============================================================================

message(STATUS "")
message(STATUS "========== NetworkModule Build Summary ==========")
message(STATUS "Build type:              ${CMAKE_BUILD_TYPE}")
message(STATUS "CMake version:           ${CMAKE_VERSION}")
message(STATUS "Compiler:                ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "")
message(STATUS "Platform Detection:")
message(STATUS "  Windows:               ${PLATFORM_WINDOWS}")
message(STATUS "  Linux:                 ${PLATFORM_LINUX}")
message(STATUS "  macOS:                 ${PLATFORM_MACOS}")
message(STATUS "")
message(STATUS "Backend Support:")
message(STATUS "  RIO (Windows):         ${NETWORK_MODULE_ENABLE_RIO}")
message(STATUS "  IOCP (Windows):        ${NETWORK_MODULE_ENABLE_IOCP}")
message(STATUS "  io_uring (Linux):      ${NETWORK_MODULE_ENABLE_IOURING}")
message(STATUS "  epoll (Linux):         ${NETWORK_MODULE_ENABLE_EPOLL}")
message(STATUS "  kqueue (macOS):        ${NETWORK_MODULE_ENABLE_KQUEUE}")
message(STATUS "")
message(STATUS "Build Options:")
message(STATUS "  Testing:               ${NETWORK_MODULE_ENABLE_TESTING}")
message(STATUS "  Benchmarks:            ${NETWORK_MODULE_ENABLE_BENCHMARKS}")
message(STATUS "  Examples:              ${NETWORK_MODULE_ENABLE_EXAMPLES}")
message(STATUS "  Shared libraries:      ${BUILD_SHARED_LIBS}")
message(STATUS "")
message(STATUS "Build output:")
message(STATUS "  Binary:                ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "  Library:               ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
message(STATUS "  Archive:               ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}")
message(STATUS "================================================")
message(STATUS "")
```

---

## 플랫폼별 조건부 컴파일

### cmake/PlatformDetection.cmake

```cmake
# =============================================================================
# 플랫폼 감지 및 기본 설정
# =============================================================================

# 플랫폼 판별
set(PLATFORM_WINDOWS FALSE)
set(PLATFORM_LINUX FALSE)
set(PLATFORM_MACOS FALSE)

if(WIN32)
    set(PLATFORM_WINDOWS TRUE)
    message(STATUS "Platform detected: Windows")
elseif(APPLE)
    set(PLATFORM_MACOS TRUE)
    message(STATUS "Platform detected: macOS")
elseif(UNIX AND NOT APPLE)
    set(PLATFORM_LINUX TRUE)
    message(STATUS "Platform detected: Linux")
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

# =============================================================================
# Windows 플랫폼 설정
# =============================================================================

if(PLATFORM_WINDOWS)
    # Windows 최소 버전 (Windows 7+)
    add_compile_definitions(_WIN32_WINNT=0x0601)
    
    # 필수 윈도우 라이브러리
    set(NETWORK_MODULE_PLATFORM_LIBS ws2_32 mswsock advapi32 kernel32)
    
    # RIO 지원 (Windows 8+)
    if(NETWORK_MODULE_ENABLE_RIO)
        # Windows 8+에서만 RIO 사용 가능
        # _WIN32_WINNT >= 0x0602 (Windows 8)
        add_compile_definitions(_RIO_ENABLED)
        message(STATUS "  RIO backend: ENABLED")
    endif()
    
    # IOCP는 모든 Windows에서 지원
    set(NETWORK_MODULE_ENABLE_IOCP ON)
    add_compile_definitions(_IOCP_ENABLED)
    message(STATUS "  IOCP backend: ENABLED")

# =============================================================================
# Linux 플랫폼 설정
# =============================================================================

elseif(PLATFORM_LINUX)
    # Linux 최소 커널 버전 감지
    execute_process(
        COMMAND uname -r
        OUTPUT_VARIABLE LINUX_KERNEL_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    message(STATUS "  Linux kernel: ${LINUX_KERNEL_VERSION}")
    
    # io_uring 지원 감지 (Linux 5.1+)
    if(NETWORK_MODULE_ENABLE_IOURING)
        # io_uring 헤더 검사
        include(CheckIncludeFile)
        check_include_file(liburing.h HAVE_LIBURING_H)
        
        if(HAVE_LIBURING_H)
            add_compile_definitions(_IOURING_ENABLED)
            set(NETWORK_MODULE_PLATFORM_LIBS ${NETWORK_MODULE_PLATFORM_LIBS} uring)
            message(STATUS "  io_uring backend: ENABLED")
        else()
            message(STATUS "  io_uring backend: DISABLED (liburing-dev not found)")
            set(NETWORK_MODULE_ENABLE_IOURING OFF)
        endif()
    endif()
    
    # epoll은 모든 Linux에서 지원
    set(NETWORK_MODULE_ENABLE_EPOLL ON)
    add_compile_definitions(_EPOLL_ENABLED)
    message(STATUS "  epoll backend: ENABLED")

# =============================================================================
# macOS 플랫폼 설정
# =============================================================================

elseif(PLATFORM_MACOS)
    # macOS 최소 버전
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.12" CACHE STRING "Minimum macOS version")
    
    # kqueue는 모든 macOS에서 지원
    if(NETWORK_MODULE_ENABLE_KQUEUE)
        add_compile_definitions(_KQUEUE_ENABLED)
        message(STATUS "  kqueue backend: ENABLED")
    endif()
    
endif()

# =============================================================================
# 공통 설정
# =============================================================================

# 필수 시스템 라이브러리
set(NETWORK_MODULE_PLATFORM_LIBS ${NETWORK_MODULE_PLATFORM_LIBS} pthread)
```

### cmake/CompilerSettings.cmake

```cmake
# =============================================================================
# 컴파일러별 설정
# =============================================================================

# MSVC (Visual Studio)
if(MSVC)
    add_compile_options(/W4 /WX)  # Level 4 warnings, treat as errors
    add_compile_options(/permissive-)  # Stricter C++ compliance
    add_compile_options(/std:c++17)
    
    # Debug/Release 설정
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Zi /Od /RTC1")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /Oi /Oy")
    
# GCC/Clang
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
    add_compile_options(-fPIC)
    
    # Debug/Release 설정
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -fsanitize=address,undefined")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
    
    # Clang 특정 설정
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        add_compile_options(-fcolor-diagnostics)
    endif()
endif()
```

### cmake/Dependencies.cmake

```cmake
# =============================================================================
# 의존성 관리
# =============================================================================

# GTest (테스트)
if(NETWORK_MODULE_ENABLE_TESTING)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.13.0.zip
    )
    FetchContent_MakeAvailable(googletest)
    message(STATUS "GTest: FOUND (FetchContent)")
endif()

# benchmark (성능 측정)
if(NETWORK_MODULE_ENABLE_BENCHMARKS)
    FetchContent_Declare(
        benchmark
        URL https://github.com/google/benchmark/archive/refs/tags/v1.8.3.zip
    )
    FetchContent_MakeAvailable(benchmark)
    message(STATUS "Google Benchmark: FOUND (FetchContent)")
endif()
```

---

## 빌드 옵션 설정

### 빌드 명령어 (Cross-Platform)

#### Windows (Visual Studio)

```bash
# 디렉토리 생성
mkdir build
cd build

# CMake 생성 (RIO 활성화, 테스트 포함)
cmake .. -G "Visual Studio 17 2022" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DNETWORK_MODULE_ENABLE_TESTING=ON ^
    -DNETWORK_MODULE_ENABLE_RIO=ON

# 빌드
cmake --build . --config Release -j 8

# 테스트 실행
ctest -C Release
```

#### Linux (Ninja/Makefile)

```bash
# 디렉토리 생성
mkdir build && cd build

# CMake 생성 (io_uring + epoll)
cmake .. -G "Ninja" \
    -DCMAKE_BUILD_TYPE=Release \
    -DNETWORK_MODULE_ENABLE_TESTING=ON \
    -DNETWORK_MODULE_ENABLE_IOURING=ON \
    -DNETWORK_MODULE_ENABLE_EPOLL=ON

# 빌드
cmake --build . -j 8

# 테스트 실행
ctest --verbose
```

#### macOS (Xcode/Ninja)

```bash
# Ninja로 빌드
mkdir build && cd build
cmake .. -G "Ninja" \
    -DCMAKE_BUILD_TYPE=Release \
    -DNETWORK_MODULE_ENABLE_TESTING=ON \
    -DNETWORK_MODULE_ENABLE_KQUEUE=ON

cmake --build . -j 8
ctest --verbose
```

### 빌드 옵션 목록

| 옵션 | 기본값 | 설명 |
|------|-------|------|
| `BUILD_SHARED_LIBS` | ON | 동적 라이브러리 빌드 |
| `CMAKE_BUILD_TYPE` | Release | Debug/Release |
| `NETWORK_MODULE_ENABLE_TESTING` | ON | 단위 테스트 포함 |
| `NETWORK_MODULE_ENABLE_BENCHMARKS` | ON | 벤치마크 포함 |
| `NETWORK_MODULE_ENABLE_RIO` | ON | Windows RIO 백엔드 |
| `NETWORK_MODULE_ENABLE_IOURING` | ON | Linux io_uring 백엔드 |
| `NETWORK_MODULE_ENABLE_EPOLL` | ON | Linux epoll 백엔드 |
| `NETWORK_MODULE_ENABLE_KQUEUE` | ON | macOS kqueue 백엔드 |
| `NETWORK_MODULE_ENABLE_EXAMPLES` | ON | 예제 프로그램 |

---

## 테스트 및 벤치마크

### src/CMakeLists.txt (라이브러리)

```cmake
# AsyncIO 라이브러리 추가
add_library(NetworkModule
    AsyncIO/AsyncIOProvider.cpp
    AsyncIO/PlatformFactory.cpp
    AsyncIO/Common/PlatformDetect.cpp
)

target_include_directories(NetworkModule PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../include"
)

# 플랫폼별 소스 추가
if(PLATFORM_WINDOWS)
    target_sources(NetworkModule PRIVATE
        AsyncIO/Platform/Windows/IocpAsyncIOProvider.cpp
    )
    if(NETWORK_MODULE_ENABLE_RIO)
        target_sources(NetworkModule PRIVATE
            AsyncIO/Platform/Windows/RIOAsyncIOProvider.cpp
        )
    endif()
elseif(PLATFORM_LINUX)
    target_sources(NetworkModule PRIVATE
        AsyncIO/Platform/Linux/EpollAsyncIOProvider.cpp
    )
    if(NETWORK_MODULE_ENABLE_IOURING)
        target_sources(NetworkModule PRIVATE
            AsyncIO/Platform/Linux/IOUringAsyncIOProvider.cpp
        )
    endif()
elseif(PLATFORM_MACOS)
    target_sources(NetworkModule PRIVATE
        AsyncIO/Platform/macOS/KqueueAsyncIOProvider.cpp
    )
endif()

# 플랫폼 라이브러리 링크
target_link_libraries(NetworkModule PRIVATE
    ${NETWORK_MODULE_PLATFORM_LIBS}
)

# 컴파일 정의
target_compile_definitions(NetworkModule PRIVATE
    NETWORK_MODULE_VERSION="1.0.0"
)
```

### test/CMakeLists.txt

```cmake
# 테스트 실행 파일
add_executable(AsyncIOTests
    AsyncIOProviderTest.cpp
    PlatformFactoryTest.cpp
    WindowsIOCPTest.cpp
    LinuxIOUringTest.cpp
    macOSKqueueTest.cpp
)

# 테스트 타겟 설정
target_link_libraries(AsyncIOTests PRIVATE
    NetworkModule
    gtest
    gtest_main
)

# 테스트 등록
gtest_discover_tests(AsyncIOTests)

# 커버리지 옵션 (선택사항)
if(ENABLE_COVERAGE)
    target_compile_options(AsyncIOTests PRIVATE --coverage)
    target_link_options(AsyncIOTests PRIVATE --coverage)
endif()
```

### benchmark/CMakeLists.txt

```cmake
# 벤치마크 실행 파일
add_executable(ThroughputBench
    ThroughputBench.cpp
)

add_executable(LatencyBench
    LatencyBench.cpp
)

# 링크 설정
foreach(target ThroughputBench LatencyBench)
    target_link_libraries(${target} PRIVATE
        NetworkModule
        benchmark::benchmark
    )
endforeach()
```

---

## 배포 및 설치

### 설치 명령어

```bash
# 빌드 및 설치
cmake --build . --target install

# 설치 위치 지정
cmake --install . --prefix /usr/local
```

### CPack 설정 (선택사항)

루트 CMakeLists.txt 마지막에 추가:

```cmake
# =============================================================================
# CPack (패키징)
# =============================================================================

include(InstallRequiredSystemLibraries)

set(CPACK_GENERATOR "ZIP" "TGZ")
set(CPACK_PACKAGE_NAME "NetworkModule")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_DESCRIPTION "Cross-platform network I/O library")
set(CPACK_PACKAGE_FILE_NAME "NetworkModule-${CPACK_PACKAGE_VERSION}")

include(CPack)
```

사용:
```bash
cpack -G ZIP    # Windows ZIP
cpack -G TGZ    # Linux/macOS tar.gz
```

---

## 문제 해결

### 1. io_uring 헤더를 찾을 수 없음 (Linux)

```bash
# liburing 설치
sudo apt-get install liburing-dev  # Ubuntu/Debian
sudo dnf install liburing-devel    # Fedora/RHEL
```

### 2. Visual Studio 생성 실패 (Windows)

```bash
# 올바른 생성기 확인
cmake --version

# 명시적 생성기 지정
cmake .. -G "Visual Studio 17 2022"
cmake .. -G "Visual Studio 16 2019"
```

### 3. macOS에서 Xcode 관련 오류

```bash
# Xcode 커맨드라인 도구 설치
xcode-select --install

# 또는 Ninja 사용
brew install ninja
cmake .. -G "Ninja"
```

### 4. 캐시 재설정

```bash
# 전체 캐시 삭제
rm -rf build/*
cmake .. -DCMAKE_BUILD_TYPE=Release
```

---

## 검증 체크리스트

- ✅ CMakeLists.txt가 최소 3.15 버전 명시
- ✅ 플랫폼별 조건부 컴파일 구현 (Windows/Linux/macOS)
- ✅ 의존성 자동 다운로드 (GTest, benchmark)
- ✅ 빌드 타입별 최적화 플래그 설정
- ✅ 테스트 자동 발견 (gtest_discover_tests)
- ✅ 설치 설정 포함 (install target)
- ✅ 빌드 정보 출력 (cmake 실행 후 요약)
- ✅ 크로스 플랫폼 빌드 명령어 제공

---

## 다음 단계

1. **로컬 테스트**: 각 플랫폼에서 빌드 및 테스트 수행
2. **CI/CD 통합**: GitHub Actions/GitLab CI 구성
3. **문서 배포**: 설치 가능한 바이너리 제공
4. **성능 검증**: 벤치마크 지표와 비교

---

**작성자**: AI Documentation  
**마지막 수정**: 2026-01-27  
**상태**: 검토 대기 중
