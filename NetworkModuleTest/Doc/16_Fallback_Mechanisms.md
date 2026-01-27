# Fallback Mechanisms and Runtime Error Recovery

**작성일**: 2026-01-27  
**버전**: 1.0  
**대상**: AsyncIOProvider 구현 및 통합 엔지니어  
**목표**: 런타임 실패 시 우아한 폴백(Graceful Fallback) 전략

---

## 📋 목차

1. [개요](#개요)
2. [폴백 전략](#폴백-전략)
3. [플랫폼별 폴백 경로](#플랫폼별-폴백-경로)
4. [구현 패턴](#구현-패턴)
5. [에러 분류 및 대응](#에러-분류-및-대응)
6. [테스트 전략](#테스트-전략)

---

## 개요

### 폴백이란?

**폴백(Fallback)**은 현재 선택한 구현이 실패했을 때 대체 구현으로 자동 전환하는 메커니즘입니다.

```
선호 구현        대체 구현 1      대체 구현 2      마지막 수단
┌────────┐      ┌──────────┐    ┌──────────┐    ┌──────────┐
│ RIO    │──X──→│ IOCP     │──X→│ Fallback │──X→│ Reject   │
└────────┘      └──────────┘    └──────────┘    └──────────┘
   ↓ (성공)
 Request
```

### 폴백이 필요한 이유

1. **런타임 기능 부재**: Windows 8에서 RIO 사용 불가
2. **리소스 고갈**: 버퍼 등록 실패
3. **I/O 오류**: 일시적 네트워크 장애
4. **커널 제약**: io_uring 파일 디스크립터 한계

### 폴백 우선순위 결정 원칙

```
점수 = (성능 × 0.5) + (호환성 × 0.3) + (구현_복잡도 × 0.2)

Windows:
  - RIO (점수 9.5): 성능↑ 단 호환성↓
  - IOCP (점수 8.0): 균형
  - Fallback (점수 3.0): 호환성만 높음

Linux:
  - io_uring (점수 9.2): 성능↑
  - epoll (점수 8.5): 균형
  - Fallback (점수 2.0): 무식하고 느림

macOS:
  - kqueue (점수 9.0): 역사적 최고
  - Fallback (점수 3.0): 호환성
```

---

## 폴백 전략

### 1. 초기화 단계 폴백

```cpp
// ✅ 초기화 시점에 최적 백엔드 선택
class AsyncIOProviderFactory
{
public:
    static std::unique_ptr<AsyncIOProvider> Create()
    {
        #ifdef _WIN32
            // Windows: RIO → IOCP → Fallback
            if(IsWindows8OrLater())
            {
                auto provider = std::make_unique<RIOAsyncIOProvider>();
                if(provider->Initialize())
                    return provider;
                // RIO 초기화 실패 → IOCP로 폴백
            }
            
            auto provider = std::make_unique<IocpAsyncIOProvider>();
            if(provider->Initialize())
                return provider;
            // IOCP 초기화 실패 → Fallback
            
            return std::make_unique<FallbackAsyncIOProvider>();
            
        #elif __linux__
            // Linux: io_uring → epoll → Fallback
            if(IsLinuxKernelVersion51OrLater())
            {
                auto provider = std::make_unique<IOUringAsyncIOProvider>();
                if(provider->Initialize())
                    return provider;
            }
            
            auto provider = std::make_unique<EpollAsyncIOProvider>();
            if(provider->Initialize())
                return provider;
            
            return std::make_unique<FallbackAsyncIOProvider>();
            
        #elif __APPLE__
            // macOS: kqueue → Fallback
            auto provider = std::make_unique<KqueueAsyncIOProvider>();
            if(provider->Initialize())
                return provider;
            
            return std::make_unique<FallbackAsyncIOProvider>();
        #endif
    }
};
```

### 2. 작업 단계 폴백 (Adaptive Fallback)

특정 작업이 실패했을 때만 폴백:

```cpp
// ✅ SendAsync 작업 단계 폴백
bool RIOAsyncIOProvider::SendAsync(
    SocketHandle socket,
    const void* data,
    uint32_t size,
    void* userContext,
    uint32_t flags,
    CompletionCallback callback
) override
{
    // 1단계: RIO로 시도
    int ret = RIOSend(
        mRioSocket,
        &wsaBuf,
        1,
        RIO_MSG_DONT_POST,
        &sendRequest
    );
    
    if(ret != SOCKET_ERROR)
        return true;  // 성공
    
    // RIO 실패 → IOCP 폴백
    DWORD dwError = WSAGetLastError();
    
    switch(dwError)
    {
    case WSAEINVAL:
    case WSAENOBUFS:
        // RIO 버퍼 문제 → IOCP로 폴백
        return FallbackToIOCP(socket, data, size, userContext, flags, callback);
        
    case WSAENOTCONN:
        // 연결 실패 → 에러 반환
        if(callback)
            callback({AsyncIOType::Send, 0, dwError, userContext}, userContext);
        return false;
        
    default:
        // 예상치 못한 에러 → IOCP 폴백
        return FallbackToIOCP(socket, data, size, userContext, flags, callback);
    }
}

// ✅ IOCP로 폴백
bool RIOAsyncIOProvider::FallbackToIOCP(
    SocketHandle socket,
    const void* data,
    uint32_t size,
    void* userContext,
    uint32_t flags,
    CompletionCallback callback
) 
{
    if(!mFallbackProvider)
    {
        mFallbackProvider = std::make_unique<IocpAsyncIOProvider>(mCompletionPort);
    }
    
    return mFallbackProvider->SendAsync(socket, data, size, userContext, flags, callback);
}
```

---

## 플랫폼별 폴백 경로

### Windows

#### 폴백 체인

```
RIO (Windows 8+, 최고 성능)
  ↓ (초기화 실패 또는 리소스 고갈)
IOCP (모든 Windows, 안정적)
  ↓ (IOCP 포트 생성 실패)
Fallback (호환성 모드, 느림)
```

#### RIO → IOCP 폴백 조건

```cpp
enum class RIOFailureReason
{
    NOT_SUPPORTED,          // Windows < 8
    BUFFER_REGISTRATION_FAILED,  // RIORegisterBuffer 실패
    RESOURCE_EXHAUSTION,    // 버퍼 한계 도달
    PERFORMANCE_DEGRADATION // 성능 저하 감지
};

// ✅ 상황별 폴백 결정
if(reason == RIOFailureReason::NOT_SUPPORTED ||
   reason == RIOFailureReason::BUFFER_REGISTRATION_FAILED)
{
    // 즉시 폴백: 이 플랫폼에서는 RIO 불가능
    mFallbackMode = true;
}
else if(reason == RIOFailureReason::RESOURCE_EXHAUSTION)
{
    // 동적 폴백: 이번 요청만 폴백, 다음에 다시 시도
    return FallbackToIOCP(...);
}
```

#### RIO 버퍼 고갈 대응

```cpp
// ✅ 버퍼 등록 실패 처리
int64_t RIOAsyncIOProvider::RegisterBuffer(const void* buffer, uint32_t size)
{
    RIO_BUFFERID bufferId = RIORegisterBuffer(
        const_cast<void*>(buffer),
        size
    );
    
    if(bufferId == RIO_INVALID_BUFFERID)
    {
        DWORD dwError = GetLastError();
        
        if(dwError == ERROR_INVALID_PARAMETER)
        {
            // 버퍼 한계 도달 → 기존 버퍼 정리
            EvictLRUBuffer();
            
            // 재시도
            bufferId = RIORegisterBuffer(
                const_cast<void*>(buffer),
                size
            );
            
            if(bufferId == RIO_INVALID_BUFFERID)
            {
                // 여전히 실패 → IOCP 폴백
                mUseRIOFallback = true;
                return -1;  // 버퍼 등록 포기
            }
        }
    }
    
    return static_cast<int64_t>(bufferId);
}
```

### Linux

#### 폴백 체인

```
io_uring (Linux 5.1+, 최고 성능)
  ↓ (커널 5.1 미만 또는 초기화 실패)
epoll (Linux 2.6+, 역사적 안정성)
  ↓ (epoll_create 실패)
Fallback (select 기반, 매우 느림)
```

#### io_uring → epoll 폴백 조건

```cpp
class IOUringAsyncIOProvider : public AsyncIOProvider
{
private:
    EpollAsyncIOProvider* mFallbackProvider = nullptr;
    bool mUseEpollFallback = false;
    
public:
    bool Initialize() override
    {
        io_uring_params params = {};
        
        if(io_uring_queue_init_params(
            QUEUE_DEPTH,
            &mRing,
            &params
        ) < 0)
        {
            // io_uring 초기화 실패 → epoll 사용
            mFallbackProvider = new EpollAsyncIOProvider();
            mUseEpollFallback = true;
            return mFallbackProvider->Initialize();
        }
        
        return true;
    }
    
    bool SendAsync(...) override
    {
        if(mUseEpollFallback)
            return mFallbackProvider->SendAsync(...);
        
        auto sqe = io_uring_get_sqe(&mRing);
        if(!sqe)
        {
            // SQE 부족 → epoll 폴백
            if(mFallbackProvider == nullptr)
            {
                mFallbackProvider = new EpollAsyncIOProvider();
                mUseEpollFallback = true;
            }
            return mFallbackProvider->SendAsync(...);
        }
        
        io_uring_prep_send(sqe, socket, data, size, flags);
        return true;
    }
};
```

#### io_uring 파일 디스크립터 한계

```cpp
// ✅ FD 한계 모니터링
class IOUringAsyncIOProvider
{
private:
    static const int FD_LIMIT_WARNING = 900;  // 1024 중 900
    int mCurrentFDCount = 0;
    
public:
    bool RegisterSocket(int fd) override
    {
        mCurrentFDCount++;
        
        if(mCurrentFDCount > FD_LIMIT_WARNING)
        {
            // 경고: FD 부족
            if(ShouldFallback())
            {
                mUseEpollFallback = true;
                return mFallbackProvider->RegisterSocket(fd);
            }
        }
        
        return true;
    }
    
private:
    bool ShouldFallback()
    {
        // 1. 새로운 연결 거부
        // 2. 또는 graceful 폐쇄 시작
        return true;
    }
};
```

### macOS

#### 폴백 체인

```
kqueue (macOS 10.0+, 네이티브)
  ↓ (kevent 필터 개수 제한)
Fallback (select 기반, 호환성)
```

---

## 구현 패턴

### Pattern 1: 팩토리 기반 폴백

```cpp
// ✅ 팩토리가 최적 구현 선택
class AsyncIOProviderFactory
{
private:
    static std::unique_ptr<AsyncIOProvider> TryCreateRIO()
    {
        if(!IsWindows8OrLater())
            return nullptr;
        
        auto provider = std::make_unique<RIOAsyncIOProvider>();
        return provider->Initialize() ? std::move(provider) : nullptr;
    }
    
    static std::unique_ptr<AsyncIOProvider> TryCreateIOCP()
    {
        auto provider = std::make_unique<IocpAsyncIOProvider>();
        return provider->Initialize() ? std::move(provider) : nullptr;
    }
    
public:
    static std::unique_ptr<AsyncIOProvider> Create(bool preferHighPerformance = true)
    {
        #ifdef _WIN32
            if(preferHighPerformance)
            {
                auto p = TryCreateRIO();
                if(p) return p;
            }
            
            auto p = TryCreateIOCP();
            if(p) return p;
            
            return std::make_unique<FallbackAsyncIOProvider>();
        #endif
    }
};
```

### Pattern 2: 데코레이터 기반 폴백

```cpp
// ✅ 데코레이터로 폴백 로직 캡슐화
class FallbackWrapper : public AsyncIOProvider
{
private:
    std::unique_ptr<AsyncIOProvider> mPrimary;
    std::unique_ptr<AsyncIOProvider> mFallback;
    
public:
    FallbackWrapper(
        std::unique_ptr<AsyncIOProvider> primary,
        std::unique_ptr<AsyncIOProvider> fallback
    ) : mPrimary(std::move(primary)), mFallback(std::move(fallback))
    {
    }
    
    bool SendAsync(
        SocketHandle socket,
        const void* data,
        uint32_t size,
        void* userContext,
        uint32_t flags,
        CompletionCallback callback
    ) override
    {
        // 1차 시도
        if(mPrimary->SendAsync(socket, data, size, userContext, flags, callback))
            return true;
        
        // 폴백
        if(mFallback)
            return mFallback->SendAsync(socket, data, size, userContext, flags, callback);
        
        return false;
    }
};

// 사용
auto primary = std::make_unique<RIOAsyncIOProvider>();
auto fallback = std::make_unique<IocpAsyncIOProvider>();
auto wrapped = std::make_unique<FallbackWrapper>(
    std::move(primary),
    std::move(fallback)
);
```

### Pattern 3: 체인 오브 레스폰서빌리티

```cpp
// ✅ 체인 패턴으로 폴백 자동화
class AsyncIOProviderChain
{
private:
    std::vector<std::unique_ptr<AsyncIOProvider>> mProviders;
    size_t mCurrentIndex = 0;
    
public:
    void AddProvider(std::unique_ptr<AsyncIOProvider> provider)
    {
        mProviders.push_back(std::move(provider));
    }
    
    bool SendAsync(
        SocketHandle socket,
        const void* data,
        uint32_t size,
        void* userContext,
        uint32_t flags,
        CompletionCallback callback
    ) override
    {
        for(size_t i = mCurrentIndex; i < mProviders.size(); ++i)
        {
            if(mProviders[i]->SendAsync(socket, data, size, userContext, flags, callback))
            {
                mCurrentIndex = i;  // 성공한 제공자 기억
                return true;
            }
        }
        return false;
    }
};

// 사용
AsyncIOProviderChain chain;
chain.AddProvider(std::make_unique<RIOAsyncIOProvider>());
chain.AddProvider(std::make_unique<IocpAsyncIOProvider>());
chain.AddProvider(std::make_unique<FallbackAsyncIOProvider>());

// 첫 성공한 제공자를 기억하여 다음 요청은 빠르게 처리
```

---

## 에러 분류 및 대응

### 복구 가능 에러 (Recoverable)

```cpp
enum class RecoverableError
{
    BUFFER_REGISTRATION_FAILED,  // 리소스 고갈 → 폴백
    TEMPORARY_SOCKET_ERROR,      // 일시적 → 재시도
    RESOURCE_EXHAUSTION,         // 리소스 부족 → 폴백
};

// ✅ 복구 전략
void HandleRecoverableError(RecoverableError err)
{
    switch(err)
    {
    case RecoverableError::BUFFER_REGISTRATION_FAILED:
        // 기존 버퍼 정리 후 재시도
        EvictLRUBuffer();
        return;
        
    case RecoverableError::TEMPORARY_SOCKET_ERROR:
        // 재시도 (exponential backoff)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return;
        
    case RecoverableError::RESOURCE_EXHAUSTION:
        // 폴백 모드 전환
        EnableFallback();
        return;
    }
}
```

### 복구 불가능 에러 (Unrecoverable)

```cpp
enum class UnrecoverableError
{
    PLATFORM_NOT_SUPPORTED,      // 플랫폼 미지원
    INVALID_CONFIGURATION,       // 잘못된 설정
    CRITICAL_KERNEL_ERROR,       // 커널 오류
};

// ✅ 에러 처리
void HandleUnrecoverableError(UnrecoverableError err)
{
    switch(err)
    {
    case UnrecoverableError::PLATFORM_NOT_SUPPORTED:
        // Fallback 제공자 사용
        UseDefaultProvider();
        break;
        
    case UnrecoverableError::INVALID_CONFIGURATION:
        // 에러 로깅 후 거절
        LogError("Invalid configuration");
        RejectedRequest();
        break;
        
    case UnrecoverableError::CRITICAL_KERNEL_ERROR:
        // 심각한 에러 → 서비스 중단
        ShutdownProvider();
        break;
    }
}
```

---

## 테스트 전략

### 테스트 1: 폴백 강제 실행

```cpp
TEST(FallbackTest, ForcePrimaryFailure)
{
    // RIO를 강제로 실패시킴
    class FailingRIOProvider : public RIOAsyncIOProvider
    {
        bool SendAsync(...) override
        {
            return false;  // 항상 실패
        }
    };
    
    auto primary = std::make_unique<FailingRIOProvider>();
    auto fallback = std::make_unique<IocpAsyncIOProvider>();
    auto wrapped = std::make_unique<FallbackWrapper>(
        std::move(primary),
        std::move(fallback)
    );
    
    // 폴백 제공자가 요청을 처리해야 함
    ASSERT_TRUE(wrapped->SendAsync(...));
}
```

### 테스트 2: 성능 비교

```cpp
TEST(FallbackTest, PerformanceDegradation)
{
    // RIO 대 IOCP 성능 비교
    auto rio = std::make_unique<RIOAsyncIOProvider>();
    auto iocp = std::make_unique<IocpAsyncIOProvider>();
    
    auto rioTime = MeasureLatency(rio.get());
    auto iocpTime = MeasureLatency(iocp.get());
    
    // IOCP 성능 저하는 예상됨
    ASSERT_LT(rioTime, iocpTime);  // RIO가 더 빨라야 함
}
```

### 테스트 3: 체인 검증

```cpp
TEST(FallbackTest, ChainProviders)
{
    AsyncIOProviderChain chain;
    chain.AddProvider(std::make_unique<FailingRIOProvider>());
    chain.AddProvider(std::make_unique<FailingIocpProvider>());
    chain.AddProvider(std::make_unique<FallbackAsyncIOProvider>());
    
    // 세 번째 제공자까지 가서 성공해야 함
    ASSERT_TRUE(chain.SendAsync(...));
}
```

---

## 체크리스트

- ✅ 플랫폼별 폴백 경로 정의
- ✅ 복구 가능/불가능 에러 분류
- ✅ 초기화 단계 폴백 구현
- ✅ 작업 단계 폴백 (Adaptive) 구현
- ✅ 리소스 고갈 감지 메커니즘
- ✅ 폴백 테스트 자동화
- ✅ 성능 저하 모니터링
- ✅ 에러 로깅 및 추적

---

**작성자**: AI Documentation  
**마지막 수정**: 2026-01-27  
**상태**: 검토 대기 중
