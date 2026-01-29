# Memory Leak Scenarios and Prevention Strategies

**작성일**: 2026-01-27  
**버전**: 1.0  
**대상**: AsyncIOProvider 구현 개발자  
**목표**: 메모리 누수 시나리오 식별 및 해결 방안 제시

---

## 📋 목차

1. [개요](#개요)
2. [일반적인 누수 시나리오](#일반적인-누수-시나리오)
3. [Windows (IOCP/RIO) 특화](#windows-iocp-rio-특화)
4. [Linux (epoll/io_uring) 특화](#linux-epoll-io_uring-특화)
5. [macOS (kqueue) 특화](#macos-kqueue-특화)
6. [검증 및 진단 방법](#검증-및-진단-방법)
7. [예방 best practices](#예방-best-practices)

---

## 개요

메모리 누수는 장시간 실행되는 서버 애플리케이션에서 **심각한 문제**입니다. AsyncIOProvider 구현 시 다음 영역에서 누수가 발생할 수 있습니다:

1. **완료 항목 관리**: CompletionEntry 메모리
2. **버퍼 등록**: RIO/io_uring 등록 버퍼
3. **사용자 컨텍스트**: void* userData 메모리
4. **세션 풀**: SessionPool의 미정리 세션
5. **콜백 함수**: 람다식 캡처 메모리

---

## 일반적인 누수 시나리오

### Scenario #1: CompletionEntry 메모리 누수

#### 문제 상황

```cpp
// ❌ 위험: 동적 할당한 CompletionEntry를 정리하지 않음
class IocpAsyncIOProvider
{
    uint32_t ProcessCompletions(...)
    {
        CompletionEntry* entries = new CompletionEntry[maxCount];  // 할당
        // ... GQCS 호출
        return completionCount;
        // ❌ delete[] entries; 호출 안 함!
    }
};
```

#### 영향도

- 매 호출마다 메모리 누수 (ProcessCompletions가 자주 호출되면 심각)
- 예: 1000 req/sec × 128 entries × 48 bytes = **6.1 MB/sec** 누수

#### 해결책

```cpp
// ✅ 올바른 방법 1: 스택 할당
uint32_t ProcessCompletions(
    CompletionEntry* entries,
    uint32_t maxCount,
    uint32_t timeoutMs
) override
{
    // entries는 호출자가 할당 → 책임도 호출자
    DWORD bytesTransferred;
    ULONG_PTR completionKey;
    OVERLAPPED* pOverlapped;
    
    BOOL ret = GetQueuedCompletionStatus(
        mCompletionPort,
        &bytesTransferred,
        &completionKey,
        &pOverlapped,
        timeoutMs
    );
    
    // 처리...
    return processedCount;
}

// ✅ 호출자 코드
std::vector<CompletionEntry> entries(128);
uint32_t count = provider->ProcessCompletions(
    entries.data(),
    entries.size(),
    1000
);
```

```cpp
// ✅ 올바른 방법 2: std::unique_ptr 사용
class IocpAsyncIOProvider
{
private:
    std::vector<CompletionEntry> mEntryBuffer;  // 재사용 가능
    
public:
    IocpAsyncIOProvider(uint32_t maxEntries = 512)
        : mEntryBuffer(maxEntries)
    {
    }
    
    uint32_t ProcessCompletions(...) override
    {
        // 미리 할당된 버퍼 사용 → 메모리 누수 없음
        return ProcessCompletionsInternal(
            mEntryBuffer.data(),
            mEntryBuffer.size()
        );
    }
};
```

#### 검증 코드

```cpp
// MemoryTest.cpp
TEST(IocpAsyncIOProvider, NoMemoryLeakInProcessCompletions)
{
    auto provider = CreateAsyncIOProvider();
    std::vector<CompletionEntry> entries(128);
    
    // 반복 호출하며 메모리 누수 확인
    for(int i = 0; i < 10000; ++i)
    {
        uint32_t count = provider->ProcessCompletions(
            entries.data(),
            entries.size(),
            100
        );
    }
    
    // valgrind/ASAN에서 메모리 누수 감지 안 됨
    // ==no leaks are possible==
}
```

---

### Scenario #2: 사용자 컨텍스트(userData) 메모리 누수

#### 문제 상황

```cpp
// ❌ 위험: userData 메모리를 정리하지 않음
struct RequestContext
{
    char* buffer;           // ← 동적 할당
    size_t bufferSize;
    uint32_t sessionId;
};

// 비동기 송신 요청
RequestContext* ctx = new RequestContext();
ctx->buffer = new char[4096];  // ← 누수 위험
ctx->bufferSize = 4096;

provider->SendAsync(
    socket,
    ctx->buffer,
    4096,
    ctx,  // ← userData
    0,
    MyCompletionCallback
);

// ❌ 콜백에서 ctx를 delete하지 않음
void MyCompletionCallback(const CompletionEntry& entry, void* userData)
{
    auto ctx = static_cast<RequestContext*>(userData);
    // ctx->buffer는 정리되지 않음! ← 누수
}
```

#### 영향도

- 모든 비동기 요청마다 누수
- 고부하 상황 (예: 초당 5000개 요청)에서 매우 심각
- 메모리 폭발적 증가 → OOM (Out of Memory)

#### 해결책

##### 방법 1: 명시적 cleanup

```cpp
// ✅ 올바른 방법: 콜백에서 정리
void MyCompletionCallback(const CompletionEntry& entry, void* userData)
{
    std::unique_ptr<RequestContext> ctx(
        static_cast<RequestContext*>(userData)
    );
    
    // ctx 사용
    if(entry.operationType == AsyncIOType::Send)
    {
        printf("Send completed: %u bytes\n", entry.bytesTransferred);
    }
    
    // 함수 끝에서 자동 정리
    // unique_ptr 소멸자가 delete 호출
}
```

##### 방법 2: 메모리 풀 사용 (권장)

```cpp
// ✅ 메모리 풀: 동적 할당 회피
class RequestContextPool
{
private:
    std::vector<RequestContext> mPool;
    std::queue<uint32_t> mFreeIndices;
    
public:
    RequestContextPool(uint32_t poolSize = 10000)
        : mPool(poolSize)
    {
        for(uint32_t i = 0; i < poolSize; ++i)
            mFreeIndices.push(i);
    }
    
    // 풀에서 획득
    RequestContext* Acquire()
    {
        if(mFreeIndices.empty())
            return nullptr;  // 풀 고갈
        
        uint32_t idx = mFreeIndices.front();
        mFreeIndices.pop();
        return &mPool[idx];
    }
    
    // 풀로 반환
    void Release(RequestContext* ctx)
    {
        uint32_t idx = ctx - mPool.data();
        mFreeIndices.push(idx);
    }
};

// 사용 예
RequestContextPool pool(10000);

// 비동기 송신
RequestContext* ctx = pool.Acquire();
if(!ctx) {
    // 풀 고갈 처리
    return false;
}

provider->SendAsync(socket, data, size, ctx, 0, MyCallback);

// 콜백에서
void MyCompletionCallback(const CompletionEntry& entry, void* userData)
{
    RequestContext* ctx = static_cast<RequestContext*>(userData);
    
    // 처리
    
    // ✅ 풀로 반환
    gPool.Release(ctx);
}
```

---

### Scenario #3: 등록된 버퍼 누수 (RIO/io_uring)

#### 문제 상황 (RIO)

```cpp
// ❌ 위험: 등록된 버퍼를 해제하지 않음
class RIOAsyncIOProvider
{
private:
    std::map<uint32_t, RIO_BUFFERID> mRegisteredBuffers;
    
public:
    uint32_t RegisterBuffer(const void* buffer, uint32_t size) override
    {
        RIO_BUFFERID bufferId = RIORegisterBuffer(
            const_cast<void*>(buffer),
            size
        );
        
        uint32_t id = ++mNextBufferId;
        mRegisteredBuffers[id] = bufferId;
        return id;
    }
    
    // ❌ UnregisterBuffer가 없거나 구현이 부족함
    void UnregisterBuffer(uint32_t bufferId) override
    {
        auto it = mRegisteredBuffers.find(bufferId);
        if(it != mRegisteredBuffers.end())
        {
            // ❌ RIODeregisterBuffer 호출 안 함!
            mRegisteredBuffers.erase(it);
        }
    }
};
```

#### 영향도

- RIO 내부 커널 자료구조 누수
- 시스템 리소스 고갈
- 결국 `RIORegisterBuffer` 호출 실패

#### 해결책

```cpp
// ✅ 올바른 구현
class RIOAsyncIOProvider
{
private:
    std::map<uint32_t, RIO_BUFFERID> mRegisteredBuffers;
    std::mutex mBufferMutex;
    
public:
    int64_t RegisterBuffer(const void* buffer, uint32_t size) override
    {
        RIO_BUFFERID bufferId = RIORegisterBuffer(
            const_cast<void*>(buffer),
            size
        );
        
        if(bufferId == RIO_INVALID_BUFFERID)
        {
            return -1;  // 에러
        }
        
        std::lock_guard<std::mutex> lock(mBufferMutex);
        uint32_t id = ++mNextBufferId;
        mRegisteredBuffers[id] = bufferId;
        return id;
    }
    
    // ✅ 명시적 해제
    void UnregisterBuffer(uint32_t bufferId) override
    {
        std::lock_guard<std::mutex> lock(mBufferMutex);
        
        auto it = mRegisteredBuffers.find(bufferId);
        if(it != mRegisteredBuffers.end())
        {
            RIODeregisterBuffer(it->second);  // ← 중요!
            mRegisteredBuffers.erase(it);
        }
    }
    
    // ✅ RAII 스타일 래퍼
    class RegisteredBuffer
    {
    private:
        RIOAsyncIOProvider* mProvider;
        uint32_t mBufferId;
        
    public:
        RegisteredBuffer(RIOAsyncIOProvider* provider, uint32_t id)
            : mProvider(provider), mBufferId(id) {}
        
        ~RegisteredBuffer()
        {
            if(mProvider && mBufferId != 0)
                mProvider->UnregisterBuffer(mBufferId);
        }
        
        uint32_t GetId() const { return mBufferId; }
    };
};

// 사용 예
{
    RIOAsyncIOProvider::RegisteredBuffer buffer(
        provider,
        provider->RegisterBuffer(data, size)
    );
    
    // buffer 사용
    
    // 스코프 끝에서 자동 해제
}  // buffer의 소멸자 호출 → RIODeregisterBuffer 호출
```

---

### Scenario #4: SessionPool 미정리 세션

#### 문제 상황

```cpp
// ❌ 위험: 소켓 닫힌 후에도 세션을 정리하지 않음
class IocpCore
{
private:
    SessionPool mSessionPool;
    
public:
    void OnSocketClosed(SOCKET socket)
    {
        // 세션 찾기
        auto session = mSessionPool.FindBySocket(socket);
        if(session)
        {
            closesocket(socket);
            // ❌ mSessionPool.Remove(session); 호출 안 함!
            // → 세션이 계속 메모리 차지
        }
    }
};
```

#### 영향도

- 장시간 실행 후 메모리 폭발적 증가
- SessionPool 크기 초과 시 새 연결 불가능

#### 해결책

```cpp
// ✅ RAII 스타일 세션 관리
class AutoSession
{
private:
    SessionPool* mPool;
    IocpObjectSession* mSession;
    
public:
    AutoSession(SessionPool* pool, IocpObjectSession* session)
        : mPool(pool), mSession(session) {}
    
    ~AutoSession()
    {
        if(mPool && mSession)
            mPool->Remove(mSession->GetSessionId());
    }
    
    IocpObjectSession* operator->() { return mSession; }
    IocpObjectSession* Get() { return mSession; }
};

// 사용 예
void OnSocketClosed(SOCKET socket)
{
    auto session = mSessionPool.FindBySocket(socket);
    if(session)
    {
        AutoSession autoSession(&mSessionPool, session);
        
        closesocket(socket);
        // autoSession 소멸 → 세션 자동 정리
    }
}

// ✅ 또는 명시적 정리
void OnSocketClosed(SOCKET socket)
{
    auto session = mSessionPool.FindBySocket(socket);
    if(session)
    {
        closesocket(socket);
        mSessionPool.Remove(session->GetSessionId());  // 명시적 정리
    }
}
```

---

## Windows (IOCP/RIO) 특화

### Scenario #5: OVERLAPPED 구조체 누수

```cpp
// ❌ 위험: OVERLAPPED를 동적 할당 후 정리하지 않음
void SendData(SOCKET socket, const char* data, int size)
{
    OVERLAPPED* pOverlapped = new OVERLAPPED();  // ← 누수 위험
    memset(pOverlapped, 0, sizeof(*pOverlapped));
    
    DWORD sent;
    WSASend(socket, ..., pOverlapped);
    // ❌ delete pOverlapped; 호출 안 함!
}

// ✅ 올바른 방법: 스택 할당 또는 풀
void SendData(SOCKET socket, const char* data, int size)
{
    OVERLAPPED overlapped = {};  // 스택 할당
    
    DWORD sent;
    WSASend(socket, ..., &overlapped);
    
    // 함수 끝에서 자동 정리
}
```

---

## Linux (epoll/io_uring) 특화

### Scenario #6: io_uring 제출 큐 오버플로우

```cpp
// ❌ 위험: SQE(Submission Queue Entry)를 정리하지 않음
void SendAsyncIOUring(int sockfd, const void* data, size_t size)
{
    auto sqe = io_uring_get_sqe(&ring);
    if(!sqe)
    {
        // SQE 부족 → 기존 요청 정리 필요
        // ❌ 정리 로직 없음!
        return;
    }
    
    io_uring_prep_send(sqe, sockfd, data, size, 0);
}

// ✅ 올바른 방법: SQE 풀 관리
class IOUringProvider
{
private:
    io_uring mRing;
    static const uint32_t SQE_POOL_SIZE = 4096;
    
public:
    int64_t RegisterBuffer(const void* ptr, size_t size) override
    {
        struct iovec iov = { const_cast<void*>(ptr), size };
        return io_uring_register_buffers(&mRing, &iov, 1);
    }
    
    bool SendAsync(
        int sockfd,
        const void* data,
        uint32_t size,
        void* userContext,
        uint32_t flags,
        CompletionCallback callback
    ) override
    {
        auto sqe = io_uring_get_sqe(&mRing);
        if(!sqe)
        {
            // ✅ 입력을 flush하여 공간 확보
            if(io_uring_submit(&mRing) < 0)
                return false;
            
            sqe = io_uring_get_sqe(&mRing);
            if(!sqe)
                return false;  // 여전히 공간 없음
        }
        
        io_uring_prep_send(sqe, sockfd, data, size, flags);
        io_uring_sqe_set_data(sqe, userContext);
        return true;
    }
};
```

---

## macOS (kqueue) 특화

### Scenario #7: kevent 필터 누수

```cpp
// ✅ macOS kqueue 필터 정리
class KqueueAsyncIOProvider
{
public:
    ~KqueueAsyncIOProvider()
    {
        // ✅ 등록된 필터 모두 제거
        struct kevent changelist;
        EV_SET(&changelist, -1, EVFILT_SOCK, EV_DELETE, 0, 0, NULL);
        kevent(mKqueueFd, &changelist, 1, NULL, 0, NULL);
        
        close(mKqueueFd);
    }
};
```

---

## 검증 및 진단 방법

### Valgrind (Linux)

```bash
# 메모리 누수 검사
valgrind --leak-check=full --show-leak-kinds=all ./test_program

# 출력 예:
# ==12345== LEAK SUMMARY:
# ==12345== definitely lost: 1,024 bytes in 10 blocks
# ==12345== indirectly lost: 0 bytes in 0 blocks
```

### AddressSanitizer (ASAN)

```bash
# 컴파일 옵션
clang++ -fsanitize=address -g -O1 test.cpp -o test

# 실행 (자동 검사)
./test

# 출력 예:
# =================================================================
# ==12345==ERROR: LeakSanitizer: detected memory leaks
```

### Windows (Dr. Memory)

```bash
# 설치 및 실행
drmemory.exe -- test.exe

# 메모리 누수 보고서 생성
```

### 메모리 프로파일러 (예: Heaptrack)

```bash
# Linux: heaptrack
heaptrack ./test_program
heaptrack_gui heaptrack.test_program.*.gz

# macOS: Instruments
instruments -t "Allocations" -o allocations.trace ./test_program
```

### 테스트 코드

```cpp
// MemoryLeakTest.cpp
#include <gtest/gtest.h>
#include "AsyncIOProvider.h"

class MemoryLeakTest : public ::testing::Test
{
protected:
    std::unique_ptr<AsyncIOProvider> provider;
    
    void SetUp() override
    {
        provider = CreateAsyncIOProvider();
    }
};

// 테스트: 반복 호출 시 메모리 누수 없음
TEST_F(MemoryLeakTest, NoLeakInProcessCompletions)
{
    std::vector<CompletionEntry> entries(128);
    
    for(int i = 0; i < 100000; ++i)
    {
        provider->ProcessCompletions(
            entries.data(),
            entries.size(),
            100
        );
    }
    // ASAN/Valgrind가 누수 감지하면 테스트 실패
}

// 테스트: 비동기 송신 반복 시 메모리 누수 없음
TEST_F(MemoryLeakTest, NoLeakInSendAsync)
{
    int socket = CreateTestSocket();
    char buffer[1024];
    
    for(int i = 0; i < 10000; ++i)
    {
        auto ctx = std::make_unique<RequestContext>();
        provider->SendAsync(
            socket,
            buffer,
            sizeof(buffer),
            ctx.release(),  // ctx 소유권 이동
            0,
            [](const CompletionEntry& e, void* ud)
            {
                auto ctx = std::unique_ptr<RequestContext>(
                    static_cast<RequestContext*>(ud)
                );
            }
        );
    }
}
```

---

## 예방 best practices

### 1. 스택 할당 우선

```cpp
// ✅ 좋음: 스택 할당
OVERLAPPED ov = {};

// ❌ 피해야 할 것: 동적 할당
OVERLAPPED* pov = new OVERLAPPED();
```

### 2. RAII 패턴 필수

```cpp
// ✅ 좋음: RAII
std::unique_ptr<RequestContext> ctx(new RequestContext());

// ✅ 좋음: 스마트 포인터
std::shared_ptr<Buffer> buffer = std::make_shared<Buffer>(4096);

// ❌ 피해야 할 것: 원시 포인터
RequestContext* ctx = new RequestContext();  // 누구가 delete? 불명확
```

### 3. 메모리 풀 사용

```cpp
// ✅ 좋음: 고정 크기 풀
class ObjectPool
{
private:
    std::vector<Object> mObjects;
    std::queue<Object*> mFreeList;
    
public:
    Object* Acquire()
    {
        if(mFreeList.empty())
            return nullptr;
        auto obj = mFreeList.front();
        mFreeList.pop();
        return obj;
    }
    
    void Release(Object* obj)
    {
        mFreeList.push(obj);
    }
};
```

### 4. 단위 테스트에 ASAN/Valgrind 통합

```cmake
# CMakeLists.txt
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-fsanitize=address)
    add_link_options(-fsanitize=address)
endif()
```

### 5. 정기적 메모리 감시

```cpp
// RuntimeMonitor.cpp
class MemoryMonitor
{
public:
    static void PrintMemoryUsage()
    {
        #ifdef _WIN32
            HANDLE hProcess = GetCurrentProcess();
            PROCESS_MEMORY_COUNTERS pmc;
            GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc));
            printf("Working Set: %zu MB\n", pmc.WorkingSetSize / (1024*1024));
        #else
            FILE* f = fopen("/proc/self/status", "r");
            // VmRSS 읽기
            fclose(f);
        #endif
    }
};

// 사용
MemoryMonitor::PrintMemoryUsage();
```

---

## 체크리스트

- ✅ 모든 동적 할당에 대응하는 해제 코드 존재
- ✅ 스마트 포인터 또는 메모리 풀 사용
- ✅ RAII 패턴 적용
- ✅ 테스트 코드에 ASAN/Valgrind 통합
- ✅ 정기적 메모리 모니터링
- ✅ 플랫폼별 특화 누수 처리 (RIO, io_uring 등)

---

**작성자**: AI Documentation  
**마지막 수정**: 2026-01-27  
**상태**: 검토 대기 중
