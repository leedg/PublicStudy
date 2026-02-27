# 단위 테스트 전략

**작성일**: 2026-01-27  
**버전**: 1.0  
**대상**: AsyncIOProvider 백엔드 구현 (RIO, io_uring, epoll)  
**목표**: 크로스 플랫폼 고품질 테스트 자동화 전략

---

## 📋 목차

1. [테스트 전략 개요](#테스트-전략-개요)
2. [테스트 프레임워크 선택](#테스트-프레임워크-선택)
3. [테스트 계층 구조](#테스트-계층-구조)
4. [단위 테스트 설계](#단위-테스트-설계)
5. [통합 테스트](#통합-테스트)
6. [성능 테스트](#성능-테스트)
7. [CI/CD 파이프라인](#cicd-파이프라인)
8. [테스트 환경 구성](#테스트-환경-구성)

---

## 테스트 전략 개요

### 목표

```
✅ 코드 커버리지: 최소 85%
✅ 플랫폼별 테스트: Windows (IOCP/RIO), Linux (epoll/io_uring)
✅ 회귀 테스트: 자동화 (모든 PR)
✅ 성능 기준선: 2.8배 향상 달성 검증
✅ 메모리 안전성: AddressSanitizer, LeakSanitizer 통과
✅ 멀티스레드 안전성: ThreadSanitizer 통과
```

### 테스트 피라미드

```
                    ▲
                   /│\
                  / │ \
                 /  │  \       E2E Tests (5%)
                /   │   \      - 실제 게임 서버 시뮬레이션
               /    │    \     - 1-2개, 장시간 실행
              ─────────────
             /  │   │   │  \   Integration Tests (15%)
            /   │   │   │   \  - 다중 백엔드 상호작용
           /    │   │   │    \ - 네트워크 시뮬레이션
          ─────────────────────
         /      │       │      \ Unit Tests (80%)
        /       │       │       \- 개별 API 테스트
       /        │       │        \- 플랫폼별 구현
      ──────────────────────────

      우리의 전략:
      - Unit 80%: AsyncIOProvider API
      - Integration 15%: 크로스 플랫폼
      - E2E 5%: 실제 시나리오
```

### 테스트 범위

| 범위 | 대상 | 테스트 타입 | 실행 빈도 |
|------|------|-----------|----------|
| **API 정확성** | AsyncIOProvider 인터페이스 | 단위 테스트 | 모든 커밋 |
| **플랫폼 구현** | RIO, io_uring, epoll | 단위 + 통합 | PR 테스트 |
| **성능** | 벤치마크 기준선 대비 회귀 | 성능 테스트 | 일일 (야간) |
| **메모리 안전성** | UAF, 메모리 누수, 버퍼 오버플로우 | 동적 분석 | PR 테스트 |
| **동시성** | 멀티스레드 race condition | ThreadSanitizer | 주 1회 |

---

## 테스트 프레임워크 선택

### 권장 프레임워크: Google Test (Gtest)

**선택 이유**:
```
✅ 크로스 플랫폼 지원 (Windows, Linux, macOS)
✅ 풍부한 ASSERT/EXPECT 매크로
✅ Fixture 지원 (테스트 설정/정리)
✅ Parameterized 테스트 (데이터 기반)
✅ 성숙한 생태계 (많은 프로젝트 채택)
✅ CI/CD 통합 용이
```

### 프레임워크 구성

```cpp
// CMakeLists.txt
find_package(GTest REQUIRED)

# 또는 Fetch 방식
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/v1.13.0.zip
)
FetchContent_MakeAvailable(googletest)

# 테스트 타겟
add_executable(asyncio_tests
    test/AsyncIOProviderTest.cpp
    test/RIOAsyncIOTest.cpp
    test/IOUringAsyncIOTest.cpp
    test/EpollAsyncIOTest.cpp
)
target_link_libraries(asyncio_tests GTest::gtest GTest::gtest_main)
```

### 추가 도구

| 도구 | 목적 | 사용 시기 |
|------|------|---------|
| **AddressSanitizer** | 메모리 에러 탐지 | 모든 테스트 실행 |
| **LeakSanitizer** | 메모리 누수 탐지 | PR 테스트 전 |
| **ThreadSanitizer** | Race condition 탐지 | 멀티스레드 테스트 |
| **Valgrind** | 상세한 메모리 분석 | 야간 테스트 |
| **gcov** | 코드 커버리지 | 주 1회 |

---

## 테스트 계층 구조

### Layer 1: 단위 테스트 (Unit Tests)

```cpp
// test/AsyncIOProviderTest.cpp

#include <gtest/gtest.h>
#include "AsyncIO/AsyncIOProvider.h"

// 영문: Test fixture for AsyncIOProvider
// 한글: AsyncIOProvider 테스트 픽스처

class AsyncIOProviderTest : public ::testing::Test
{
protected:
    std::unique_ptr<AsyncIOProvider> provider;
    
    void SetUp() override
    {
        // 영문: Create provider (platform auto-detected)
        // 한글: Provider 생성 (플랫폼 자동 감지)
        provider = CreateAsyncIOProvider();
        ASSERT_NE(nullptr, provider);
        
        // 초기화
        ASSERT_EQ(AsyncIOError::Success,
            provider->Initialize(4096, 10000));
    }
    
    void TearDown() override
    {
        if (provider && provider->IsInitialized())
            provider->Shutdown();
    }
};

// 테스트 케이스들

TEST_F(AsyncIOProviderTest, InitializationSuccess)
{
    EXPECT_TRUE(provider->IsInitialized());
}

TEST_F(AsyncIOProviderTest, DoubleInitializationFails)
{
    // 재초기화 시도
    auto result = provider->Initialize(4096, 10000);
    EXPECT_EQ(AsyncIOError::AlreadyInitialized, result);
}

TEST_F(AsyncIOProviderTest, GetInfoReturnsValidData)
{
    const auto& info = provider->GetInfo();
    EXPECT_GE(info.maxQueueDepth, 4096);
    EXPECT_GE(info.maxConcurrentReq, 10000);
    EXPECT_TRUE(info.name != nullptr);
}

TEST_F(AsyncIOProviderTest, StatsAreInitialized)
{
    auto stats = provider->GetStats();
    EXPECT_EQ(0, stats.totalRequests);
    EXPECT_EQ(0, stats.totalCompletions);
    EXPECT_EQ(0, stats.pendingRequests);
}
```

### Layer 2: 통합 테스트 (Integration Tests)

```cpp
// test/AsyncIOIntegrationTest.cpp

class AsyncIOIntegrationTest : public ::testing::Test
{
protected:
    std::unique_ptr<AsyncIOProvider> provider;
    std::pair<SOCKET, SOCKET> loopback;
    
    void SetUp() override
    {
        provider = CreateAsyncIOProvider();
        ASSERT_NE(nullptr, provider);
        ASSERT_EQ(AsyncIOError::Success,
            provider->Initialize(4096, 10000));
        
        // 영문: Create loopback socket pair
        // 한글: 루프백 소켓 쌍 생성
        loopback = CreateLoopbackSocketPair();
        ASSERT_NE(INVALID_SOCKET, loopback.first);
        ASSERT_NE(INVALID_SOCKET, loopback.second);
    }
    
    void TearDown() override
    {
        if (loopback.first != INVALID_SOCKET)
            closesocket(loopback.first);
        if (loopback.second != INVALID_SOCKET)
            closesocket(loopback.second);
        
        if (provider && provider->IsInitialized())
            provider->Shutdown();
    }
};

// 메시지 송수신 테스트
TEST_F(AsyncIOIntegrationTest, SendAndReceiveMessage)
{
    const char* testData = "Hello, AsyncIO!";
    size_t dataSize = strlen(testData);
    
    // 송신
    auto sendResult = provider->SendAsync(
        loopback.first, testData, dataSize, (RequestContext)1, 0);
    EXPECT_EQ(AsyncIOError::Success, sendResult);
    
    // 배치 실행
    provider->FlushRequests();
    
    // 수신
    std::array<CompletionEntry, 2> entries;
    int count = provider->ProcessCompletions(entries.data(), 2, 1000);
    
    // 검증
    EXPECT_EQ(1, count);
    EXPECT_EQ(1, entries[0].context);
    EXPECT_EQ(dataSize, entries[0].result);
}
```

### Layer 3: 성능 테스트 (Performance Tests)

```cpp
// test/AsyncIOPerformanceTest.cpp

class AsyncIOPerformanceTest : public ::testing::Test
{
protected:
    std::unique_ptr<AsyncIOProvider> provider;
    ProviderInfo baseline;  // 기준선
    
    void SetUp() override
    {
        provider = CreateAsyncIOProvider();
        provider->Initialize(4096, 10000);
        LoadBaseline(baseline);
    }
};

// 성능 회귀 테스트
TEST_F(AsyncIOPerformanceTest, ThroughputDoesNotRegress)
{
    // 1M 작업 실행
    const size_t NUM_OPS = 1000000;
    const size_t BATCH_SIZE = 100;
    
    auto [sock1, sock2] = CreateLoopbackPair();
    char data[4096];
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 0; i < NUM_OPS; i++)
    {
        provider->SendAsync(sock1, data, sizeof(data), i, 0);
        
        if ((i + 1) % BATCH_SIZE == 0)
            provider->FlushRequests();
    }
    
    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    double throughput = NUM_OPS / 
        std::chrono::duration<double>(elapsed).count();
    
    // 기준선 대비 5% 이상 저하 시 실패
    double expectedThroughput = baseline.expectedThroughput;
    EXPECT_GE(throughput, expectedThroughput * 0.95)
        << "Throughput regression: " << throughput 
        << " < " << (expectedThroughput * 0.95);
}
```

---

## 단위 테스트 설계

### 테스트 카테고리

#### 1. 초기화 및 정리 테스트

```cpp
TEST_F(AsyncIOProviderTest, InitializeWithInvalidQueueDepth)
{
    // 큐 깊이 0
    auto result = provider->Initialize(0, 1000);
    EXPECT_NE(AsyncIOError::Success, result);
}

TEST_F(AsyncIOProviderTest, ShutdownWhenNotInitialized)
{
    auto uninitialized = CreateAsyncIOProvider();
    EXPECT_NO_THROW(uninitialized->Shutdown());  // 안전해야 함
}

TEST_F(AsyncIOProviderTest, ReinitializeAfterShutdown)
{
    provider->Shutdown();
    auto result = provider->Initialize(4096, 10000);
    EXPECT_EQ(AsyncIOError::Success, result);
}
```

#### 2. 버퍼 관리 테스트

```cpp
TEST_F(AsyncIOProviderTest, RegisterBufferSuccess)
{
    const size_t bufferSize = 4096;
    auto buffer = std::make_unique<char[]>(bufferSize);
    
    int64_t bufferId = provider->RegisterBuffer(buffer.get(), bufferSize);
    EXPECT_GE(bufferId, 0);
    
    // 정리
    EXPECT_EQ(AsyncIOError::Success, provider->UnregisterBuffer(bufferId));
}

TEST_F(AsyncIOProviderTest, RegisterNullBuffer)
{
    int64_t bufferId = provider->RegisterBuffer(nullptr, 4096);
    EXPECT_LT(bufferId, 0);  // 실패해야 함
}

TEST_F(AsyncIOProviderTest, UnregisterInvalidBufferId)
{
    auto result = provider->UnregisterBuffer(-999);
    EXPECT_NE(AsyncIOError::Success, result);
}
```

#### 3. 비동기 작업 테스트

```cpp
TEST_F(AsyncIOProviderTest, SendAsyncWithInvalidSocket)
{
    char data[10] = "test";
    auto result = provider->SendAsync(INVALID_SOCKET, data, 10, 1, 0);
    EXPECT_EQ(AsyncIOError::InvalidSocket, result);
}

TEST_F(AsyncIOProviderTest, RecvAsyncReturnsError)
{
    char buffer[10];
    auto result = provider->RecvAsync(INVALID_SOCKET, buffer, 10, 1, 0);
    EXPECT_EQ(AsyncIOError::InvalidSocket, result);
}

TEST_F(AsyncIOProviderTest, FlushRequestsWhenNoPending)
{
    auto result = provider->FlushRequests();
    EXPECT_EQ(AsyncIOError::Success, result);  // 무해함
}
```

#### 4. 완료 처리 테스트

```cpp
TEST_F(AsyncIOProviderTest, ProcessCompletionsTimeout)
{
    std::array<CompletionEntry, 32> entries;
    auto count = provider->ProcessCompletions(entries.data(), 32, 100);
    
    // 타임아웃 시 0 반환
    EXPECT_EQ(0, count);
}

TEST_F(AsyncIOProviderTest, ProcessCompletionsWithInvalidArray)
{
    // nullptr 배열
    int count = provider->ProcessCompletions(nullptr, 32, 0);
    EXPECT_LT(count, 0);  // 에러
}

TEST_F(AsyncIOProviderTest, ProcessCompletionsZeroSize)
{
    std::array<CompletionEntry, 32> entries;
    auto count = provider->ProcessCompletions(entries.data(), 0, 0);
    EXPECT_EQ(0, count);  // 무해함
}
```

### Parameterized 테스트

```cpp
// 여러 플랫폼에서 동일한 테스트 실행

class AsyncIOProviderParamTest 
    : public ::testing::TestWithParam<const char*>
{
protected:
    std::unique_ptr<AsyncIOProvider> provider;
    
    void SetUp() override
    {
        const char* platformHint = GetParam();
        provider = CreateAsyncIOProvider(platformHint);
        provider->Initialize(4096, 10000);
    }
};

INSTANTIATE_TEST_SUITE_P(
    ProviderTests,
    AsyncIOProviderParamTest,
    ::testing::Values("IOCP", "RIO", "epoll", "io_uring")
);

TEST_P(AsyncIOProviderParamTest, BasicOperations)
{
    // 모든 플랫폼에서 동일한 테스트 실행
    const auto& info = provider->GetInfo();
    EXPECT_TRUE(provider->IsInitialized());
    EXPECT_GT(info.maxQueueDepth, 0);
}
```

---

## 통합 테스트

### 멀티 백엔드 테스트

```cpp
class MultiBackendTest : public ::testing::Test
{
protected:
    std::vector<std::pair<std::string, std::unique_ptr<AsyncIOProvider>>> backends;
    
    void SetUp() override
    {
        #ifdef _WIN32
            backends.push_back({"IOCP", CreateAsyncIOProvider("IOCP")});
            backends.push_back({"RIO", CreateAsyncIOProvider("RIO")});
        #else
            backends.push_back({"epoll", CreateAsyncIOProvider("epoll")});
            backends.push_back({"io_uring", CreateAsyncIOProvider("io_uring")});
        #endif
        
        for (auto& [name, provider] : backends)
        {
            if (provider)
                provider->Initialize(4096, 10000);
        }
    }
};

TEST_F(MultiBackendTest, AllBackendsProduceSameResults)
{
    for (const auto& [name, provider] : backends)
    {
        if (!provider) continue;
        
        auto [sock1, sock2] = CreateLoopbackPair();
        const char* testData = "test";
        
        // 모든 백엔드에서 동일한 동작 검증
        auto result = provider->SendAsync(sock1, testData, 4, 1, 0);
        EXPECT_EQ(AsyncIOError::Success, result) 
            << "Platform: " << name;
    }
}
```

### 크로스 플랫폼 호환성 테스트

```cpp
class CrossPlatformCompatibilityTest : public ::testing::Test
{
    // Windows에서 IocpCallbackAdapter 검증
    TEST(IocpAdapterTest, CallbackConversionCorrect)
    {
        IocpCallbackAdapter adapter(nullptr, MockCallback);
        
        CompletionResult result{
            .mBytesTransferred = 100,
            .mErrorCode = 0,
            .mStatus = CompletionResult::Status::Success
        };
        
        EXPECT_CALL(MockCallback, Call).Times(1);
        adapter.OnAsyncCompletion(result, nullptr);
    }
};
```

---

## 성능 테스트

### 기준선 설정

```cpp
class PerformanceBaselineTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // 성능 환경 검증
        VerifyTestEnvironment();
    }
    
    void VerifyTestEnvironment()
    {
        // CPU 클럭 속도 확인
        // 메모리 가용성 확인
        // 네트워크 안정성 확인
        // 다른 프로세스 간섭 확인
    }
};

TEST_F(PerformanceBaselineTest, EstablishBaseline)
{
    // 첫 벤치마크: 기준선 설정
    // 결과: baseline.json에 저장
    // 향후 회귀 테스트의 참고 자료
}
```

### 회귀 테스트

```cpp
TEST_F(PerformanceRegressionTest, NoThroughputRegression)
{
    auto baseline = LoadBaseline("baseline.json");
    auto current = MeasureThroughput();
    
    // 5% 이상 저하 시 실패
    EXPECT_GE(current.throughput, baseline.throughput * 0.95)
        << "Regression: " << current.throughput
        << " < " << (baseline.throughput * 0.95);
}

TEST_F(PerformanceRegressionTest, LatencyWithinBounds)
{
    auto baseline = LoadBaseline("baseline.json");
    auto current = MeasureLatency();
    
    // p99 레이턴시: 10% 악화 허용
    EXPECT_LE(current.p99_us, baseline.p99_us * 1.10)
        << "P99 latency increased beyond threshold";
}
```

---

## CI/CD 파이프라인

### GitHub Actions 설정

```yaml
# .github/workflows/test.yml

name: Unit Tests

on:
  push:
    branches: [main, develop]
  pull_request:
    branches: [main, develop]

jobs:
  test-windows:
    runs-on: windows-2022
    steps:
      - uses: actions/checkout@v3
      
      - name: Install dependencies
        run: |
          vcpkg install gtest:x64-windows
      
      - name: Build tests
        run: |
          mkdir build && cd build
          cmake .. -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
          cmake --build . --config Release
      
      - name: Run tests
        run: |
          cd build
          ./Release/asyncio_tests.exe --gtest_output=json:test_results.json
      
      - name: Upload results
        uses: actions/upload-artifact@v3
        with:
          name: windows-test-results
          path: build/test_results.json

  test-linux:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y libgtest-dev liburing-dev clang
      
      - name: Build with AddressSanitizer
        run: |
          mkdir build && cd build
          cmake .. -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer"
          cmake --build .
      
      - name: Run tests with ThreadSanitizer
        env:
          TSAN_OPTIONS: halt_on_error=1
        run: |
          cd build
          ./asyncio_tests --gtest_output=json:test_results.json

  coverage:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      
      - name: Generate coverage
        run: |
          mkdir build && cd build
          cmake .. -DCMAKE_CXX_FLAGS="--coverage"
          cmake --build .
          ./asyncio_tests
          gcov test/*.cpp
      
      - name: Upload coverage
        uses: codecov/codecov-action@v3
        with:
          files: ./build/*.gcov
```

---

## 테스트 환경 구성

### 로컬 개발 환경

```bash
# 테스트 실행 (모든 테스트)
cmake --build build --target test

# 특정 테스트만 실행
./build/asyncio_tests --gtest_filter=AsyncIOProviderTest.*

# 성능 테스트만
./build/asyncio_tests --gtest_filter="*PerformanceTest*"

# 커버리지 생성
cmake --build build --target coverage
open build/coverage/index.html
```

### 메모리 안전성 테스트

```bash
# AddressSanitizer 사용
ASAN_OPTIONS=detect_leaks=1 ./asyncio_tests

# LeakSanitizer 사용
LSAN_OPTIONS=verbosity=1 ./asyncio_tests

# Valgrind (상세 분석)
valgrind --leak-check=full --show-leak-kinds=all ./asyncio_tests
```

### 멀티스레드 안전성 테스트

```bash
# ThreadSanitizer
TSAN_OPTIONS=halt_on_error=1 ./asyncio_tests

# 여러 번 실행 (race condition 탐지)
for i in {1..10}; do
  ./asyncio_tests || break
done
```

---

## 테스트 커버리지 목표

| 항목 | 목표 | 현재 | 진행상황 |
|------|------|------|---------|
| 코드 커버리지 | 85% | - | 진행 중 |
| AsyncIOProvider API | 100% | - | 진행 중 |
| 플랫폼 구현 (각) | 80%+ | - | 진행 중 |
| 에러 경로 | 90% | - | 진행 중 |
| 엣지 케이스 | 80% | - | 진행 중 |

---

## 테스트 실행 시간

| 테스트 타입 | 목표 시간 | CI에서 실행 |
|-----------|---------|----------|
| 단위 테스트 | <30초 | 매번 |
| 통합 테스트 | <2분 | 매 PR |
| 성능 테스트 | <5분 | 일일 (야간) |
| 전체 (커버리지 포함) | <10분 | 주 1회 |

---

## 결론

이 테스트 전략은 AsyncIOProvider의 **품질, 성능, 안정성**을 보장합니다.

**주요 체크리스트**:
- [ ] GTest 프레임워크 설정
- [ ] 단위 테스트 작성 (80% 커버리지)
- [ ] 통합 테스트 구현
- [ ] CI/CD 파이프라인 설정
- [ ] 성능 기준선 설정
- [ ] 메모리 안전성 검증
- [ ] 멀티스레드 안전성 검증
- [ ] 커버리지 리포팅 활성화

**다음 단계**: Unit Test Suite 구현 (목표: Week 1)

---

**참고**: 06_Cross_Platform_Architecture.md, 07_API_Design_Document.md와 함께 사용
