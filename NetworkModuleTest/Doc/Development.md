# 개발 가이드

## 1. 개발 환경 설정

### 1.1 필수 조건
- **OS**: Windows 10+, Linux, macOS
- **컴파일러**: 
  - Windows: MSVC 2019+ or MinGW-w64 8.0+
  - Linux: GCC 7+ or Clang 5+
  - macOS: Clang 10+
- **CMake**: 3.15+
- **Git**: 2.20+

### 1.2 의존성 라이브러리
```cmake
# 기본 의존성
- C++17 표준 라이브러리
- Protocol Buffers (libprotobuf, protoc)
- Google Test (선택사항, 테스트용)

# 플랫폼별 의존성
Windows:
- Windows Sockets 2 (ws2_32.lib)
- Winsock Extensions (mswsock.lib)

Linux:
- liburing (io_uring용, 선택사항)
- pthread

macOS:
- pthread
```

### 1.3 개발 도구 설치

#### Windows
```powershell
# vcpkg로 의존성 설치
vcpkg install protobuf:x64-windows
vcpkg install gtest:x64-windows

# CMake 설정
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=[vcpkg-root]/scripts/buildsystems/vcpkg.cmake
```

#### Linux (Ubuntu/Debian)
```bash
sudo apt-get update
sudo apt-get install build-essential cmake
sudo apt-get install libprotobuf-dev protobuf-compiler
sudo apt-get install libgtest-dev

# io_uring 지원 (커널 5.1+)
sudo apt-get install liburing-dev
```

#### macOS
```bash
brew install cmake protobuf
brew install googletest
```

## 2. 프로젝트 빌드

### 2.1 기본 빌드 명령어
```bash
# 빌드 디렉토리 생성
mkdir build
cd build

# CMake 설정
cmake ..

# 빌드
cmake --build . --config Release

# 테스트 실행
ctest
```

### 2.2 플랫폼별 빌드 옵션
```bash
# Windows (Visual Studio)
cmake -G "Visual Studio 16 2019" -A x64 ..
cmake --build . --config Release

# Linux (Debug 빌드)
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j$(nproc)

# macOS
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(sysctl -n hw.ncpu)
```

### 2.3 테스트 실행
```bash
# 모든 테스트 실행
./tests/NetworkModuleTest

# 특정 테스트 실행
./tests/NetworkModuleTest --gtest_filter="AsyncIO*"

# 상세 출력
./tests/NetworkModuleTest --gtest_print_time=1
```

## 3. 프로젝트 구조

### 3.1 디렉토리 구조
```
NetworkModuleTest/
├── Doc/                           # 문서
│   ├── Architecture.md            # 아키텍처 명세
│   ├── API.md                    # API 명세
│   ├── Protocol.md               # 프로토콜 명세
│   └── Development.md            # 개발 가이드 (본 파일)
├── Server/                        # 서버 구현
│   ├── DBServer/                  # 데이터베이스 서버
│   │   ├── include/              # 헤더 파일
│   │   ├── src/                  # 소스 파일
│   │   ├── CMakeLists.txt        # 빌드 스크립트
│   │   └── main.cpp              # 메인 파일
│   ├── TestServer/                # 로직 처리 서버
│   │   ├── include/
│   │   ├── src/
│   │   ├── CMakeLists.txt
│   │   └── main.cpp
│   └── ServerEngine/             # 네트워크 엔진
│       ├── Core/                  # 핵심 인터페이스
│       │   ├── AsyncIOProvider.h/.cpp
│       │   ├── PlatformDetect.h/.cpp
│       │   └── Types.h
│       ├── Platforms/             # 플랫폼별 구현
│       │   ├── Windows/
│       │   ├── Linux/
│       │   └── macOS/
│       ├── Protocols/             # 프로토콜 모듈
│       │   ├── ping.proto
│       │   ├── PingPong.h/.cpp
│       │   └── MessageHandler.h/.cpp
│       ├── Utils/                 # 유틸리티
│       └── Tests/                 # 테스트
└── NetworkModuleTest/              # 기존 코드 (보관)
```

### 3.2 코드 규칙

#### 3.2.1 네이밍 규칙
```cpp
// 클래스: PascalCase
class AsyncIOProvider {
public:
    // 메소드: PascalCase
    void Initialize();
    
    // 변수: camelCase (private), mPascalCase (member)
    bool mInitialized;
    int32_t socketCount;
};

// 상수: UPPER_SNAKE_CASE
const int32_t MAX_CONNECTIONS = 10000;

// 네임스페이스: PascalCase
namespace Network::AsyncIO {
}

// 파일: PascalCase.h/.cpp
AsyncIOProvider.h
AsyncIOProvider.cpp
```

#### 3.2.2 코딩 스타일
```cpp
// 헤더 가드
#pragma once

// 인클루드 순서: 시스템 -> 외부 라이브러리 -> 내부 헤더
#include <cstdint>
#include <memory>
#include <vector>

#include <google/protobuf/message.h>

#include "Core/Types.h"
#include "Protocols/MessageHandler.h"

// 주석 스타일
/**
 * English: Brief description of the class/function
 * 한글: 클래스/함수에 대한 간단한 설명
 * @param paramName Parameter description
 * @return Return value description
 */

// 중괄호 스타일
if (condition) {
    // statements
} else {
    // statements
}

// 라인 길이: 최대 120자
// 들여쓰기: 4 스페이스
```

## 4. 개발 워크플로우

### 4.1 Git 워크플로우
```bash
# 1. feature 브랜치 생성
git checkout -b feature/new-feature

# 2. 개발 및 커밋
git add .
git commit -m "feat: add new feature"

# 3. 테스트
./build/tests/NetworkModuleTest

# 4. 푸시 및 PR
git push origin feature/new-feature
# GitHub에서 Pull Request 생성
```

### 4.2 커밋 메시지 규칙
```
feat: 새로운 기능 추가
fix: 버그 수정
docs: 문서 수정
style: 코드 스타일 수정 (로직 변경 없음)
refactor: 코드 리팩토링
test: 테스트 추가/수정
chore: 빌드/유틸리티 수정

예시:
feat: add JWT authentication support
fix: resolve memory leak in connection handler
docs: update API documentation
refactor: extract common utilities to Utils module
```

### 4.3 코드 리뷰 프로세스
1. **Self-Review**: PR 생성 전 자가 검토
2. **Peer Review**: 동료 개발자 리뷰
3. **Test Review**: 테스트 커버리지 확인
4. **Integration Test**: 전체 시스템 통합 테스트

## 5. 디버깅 및 테스트

### 5.1 로깅
```cpp
// 로깅 레벨
enum LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3
};

// 로그 매크로
LOG_DEBUG("Connection established: {}", connectionId);
LOG_INFO("Processing request: {}", requestType);
LOG_WARN("High latency detected: {}ms", latency);
LOG_ERROR("Database connection failed: {}", errorMessage);
```

### 5.2 단위 테스트 작성
```cpp
// AsyncIOTest.cpp
TEST(AsyncIOProviderTest, Initialization) {
    auto provider = CreateAsyncIOProvider();
    ASSERT_NE(provider, nullptr);
    
    auto result = provider->Initialize(256, 1000);
    EXPECT_EQ(result, AsyncIOError::Success);
    EXPECT_TRUE(provider->IsInitialized());
}

TEST(AsyncIOProviderTest, SendReceive) {
    auto provider = CreateAsyncIOProvider();
    ASSERT_EQ(provider->Initialize(), AsyncIOError::Success);
    
    // Test send/receive logic
    // ...
}
```

### 5.3 성능 테스트
```cpp
// PerformanceTest.cpp
TEST(PerformanceTest, ConcurrentConnections) {
    const int NUM_CONNECTIONS = 1000;
    const int MESSAGES_PER_CONNECTION = 100;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Create connections and send messages
    // ...
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    LOG_INFO("Processed {} messages in {}ms", 
             NUM_CONNECTIONS * MESSAGES_PER_CONNECTION, duration.count());
}
```

## 6. 배포

### 6.1 빌드 산출물
```
build/
├── bin/
│   ├── DBServer.exe          # 데이터베이스 서버
│   ├── TestServer.exe        # 로직 처리 서버
│   └── NetworkModuleTest.exe # 테스트 실행 파일
├── lib/
│   ├── libAsyncIO.a          # 정적 라이브러리
│   └── libProtobuf.a         # 프로토콜 라이브러리
└── include/                   # 헤더 파일
```

### 6.2 Docker 빌드 (선택사항)
```dockerfile
# Dockerfile
FROM ubuntu:20.04

# 설치
RUN apt-get update && apt-get install -y \
    build-essential cmake \
    libprotobuf-dev protobuf-compiler

# 소스 복사 및 빌드
COPY . /app
WORKDIR /app/build
RUN cmake .. && make -j$(nproc)

# 실행
CMD ["./bin/TestServer"]
```

### 6.3 환경 설정
```bash
# 환경 변수 설정
export NETWORK_LOG_LEVEL=INFO
export NETWORK_MAX_CONNECTIONS=10000
export NETWORK_TIMEOUT_MS=30000
export DB_HOST=localhost
export DB_PORT=5432
export DB_NAME=networkdb
export DB_USER=admin
export DB_PASSWORD=password
```

## 7. 모니터링 및 유지보수

### 7.1 상태 모니터링
```cpp
// 메트릭 수집
struct ServerMetrics {
    uint64_t totalConnections;
    uint64_t activeConnections;
    uint64_t totalRequests;
    uint64_t errorCount;
    double averageLatency;
    double memoryUsage;
    double cpuUsage;
};
```

### 7.2 로그 관리
```
logs/
├── server.log             # 서버 로그
├── error.log              # 에러 로그
├── access.log             # 접근 로그
├── performance.log        # 성능 로그
└── audit.log              # 감사 로그
```

### 7.3 장애 조치
```bash
# 자동 재시작 스크립트
#!/bin/bash
while true; do
    if ! pgrep -f "TestServer" > /dev/null; then
        echo "Restarting TestServer..."
        ./bin/TestServer &
    fi
    sleep 5
done
```

## 8. 확장 가이드

### 8.1 새로운 프로토콜 추가
1. `Protocols/` 디렉토리에 `.proto` 파일 생성
2. `protoc`로 코드 생성
3. 메시지 핸들러 구현
4. 테스트 케이스 추가

### 8.2 새로운 플랫폼 지원
1. `Platforms/` 하위 디렉토리 생성
2. `AsyncIOProvider` 상속 클래스 구현
3. `PlatformDetect`에 탐지 로직 추가
4. CMakeLists.txt에 플랫폼별 설정 추가

### 8.3 새로운 서버 타입 추가
1. `Server/` 하위 디렉토리 생성
2. 서버 메인 로직 구현
3. 프로토콜 핸들러 연동
4. 빌드 스크립트 수정

---

*본 개발 가이드는 프로젝트 발전에 따라 지속적으로 업데이트됩니다.*