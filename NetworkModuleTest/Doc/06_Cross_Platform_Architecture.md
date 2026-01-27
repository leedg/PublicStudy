# Cross-Platform Architecture Design for Network Module

**작성일**: 2026-01-27  
**버전**: 1.0  
**대상**: RAON Network Module AsyncIO 통일 인터페이스  
**목표**: Windows (IOCP/RIO) + Linux (epoll/io_uring) 크로스 플랫폼 설계

---

## 📋 목차

1. [전체 아키텍처](#전체-아키텍처)
2. [AsyncIOProvider 인터페이스 상세](#asyncioprovider-인터페이스-상세)
3. [Windows 구현 (RIO)](#windows-구현-rio)
4. [Linux 구현 (io_uring)](#linux-구현-io_uring)
5. [플랫폼 선택 전략](#플랫폼-선택-전략)
6. [에러 처리 전략](#에러-처리-전략)
7. [메모리 관리 전략](#메모리-관리-전략)
8. [성능 최적화 가이드](#성능-최적화-가이드)
9. [테스트 전략](#테스트-전략)

---

## 전체 아키텍처

### 계층 구조

```
┌─────────────────────────────────────────────────────────┐
│ Application Layer                                        │
│ - GameServer, IocpCore, ServiceCoordinator             │
└────────────┬────────────────────────────────────────────┘
             │
┌────────────▼────────────────────────────────────────────┐
│ AsyncIOProvider Interface (추상화 계층)                  │
│ - SendAsync(), RecvAsync(), ProcessCompletions()       │
└────────────┬────────────────────────────────────────────┘
             │
    ┌────────┴─────────┬──────────────┐
    │                  │              │
┌───▼──────────────┬───▼─────────┬───▼──────────────┐
│ Windows IOCP     │ Windows RIO │ Linux io_uring   │
├──────────────────┼─────────────┼──────────────────┤
│ - GQCS           │ - RIOSend   │ - io_uring_enter│
│ - WSASend/Recv   │ - RIORecv   │ - io_uring_prep │
│ - PostQCS        │ - RIONotify │ - CQE 처리      │
└──────────────────┴─────────────┴──────────────────┘
             │                │              │
             ▼                ▼              ▼
        Kernel (Windows)  Kernel (Windows 8+)  Kernel (Linux 5.1+)
```

### 디렉토리 구조

```
NetworkModule/
├── AsyncIO/
│   ├── AsyncIOProvider.h           (추상 인터페이스)
│   ├── AsyncIOProvider.cpp         (기본 구현)
│   ├── Platform/
│   │   ├── Windows/
│   │   │   ├── IocpAsyncIOProvider.h/cpp
│   │   │   ├── RIOAsyncIOProvider.h/cpp
│   │   │   └── RIOBufferPool.h/cpp
│   │   ├── Linux/
│   │   │   ├── EpollAsyncIOProvider.h/cpp
│   │   │   ├── IOUringAsyncIOProvider.h/cpp
│   │   │   └── IOUringBufferPool.h/cpp
│   │   └── Common/
│   │       └── PlatformDetect.h
│   ├── Test/
│   │   ├── AsyncIOTest.cpp
│   │   ├── RIOTest.cpp
│   │   └── IOUringTest.cpp
│   └── Benchmark/
│       ├── ThroughputBench.cpp
│       └── LatencyBench.cpp
├── Iocp/
│   ├── IocpCore.h/cpp            (기존, AsyncIOProvider 사용)
│   ├── IocpObjectListener.h/cpp
│   └── ...
├── Session/
│   ├── IocpObjectSession.h/cpp   (최소 변경)
│   └── ...
└── Buffer/
    └── ...
```

---

## AsyncIOProvider 인터페이스 상세

### 헤더 파일 (`AsyncIOProvider.h`)

```cpp
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#ifdef _WIN32
    #include <winsock2.h>
    using SocketHandle = SOCKET;
    using OSError = DWORD;
#else
    #include <sys/socket.h>
    using SocketHandle = int;
    using OSError = int;
#endif

namespace RAON::Network::AsyncIO
{
    // ========================================
    // 타입 정의
    // ========================================
    
    // 요청 컨텍스트 (사용자 정의 데이터)
    using RequestContext = uint64_t;
    
    // 완료 콜백 함수
    using CompletionCallback = void(*)(const struct CompletionEntry& entry, void* userData);
    
    // ========================================
    // 열거형
    // ========================================
    
    // 비동기 작업 타입
    enum class AsyncIOType : uint8_t
    {
        Send,      // 송신
        Recv,      // 수신
        Accept,    // 연결 수락 (리스너)
        Connect,   // 연결 요청 (클라이언트)
        Timeout,   // 타임아웃 (내부 사용)
        Error,     // 에러 (내부 사용)
    };
    
    // 플랫폼 타입
    enum class PlatformType : uint8_t
    {
        IOCP,      // Windows IOCP
        RIO,       // Windows Registered I/O (8+)
        Epoll,     // Linux epoll
        IOUring,   // Linux io_uring (5.1+)
        Kqueue,    // macOS kqueue (미래)
    };
    
    // 에러 코드
    enum class AsyncIOError : int32_t
    {
        Success = 0,
        NotInitialized = -1,
        InvalidSocket = -2,
        OperationPending = -3,
        OperationFailed = -4,
        InvalidBuffer = -5,
        NoResources = -6,
        Timeout = -7,
        PlatformNotSupported = -8,
        AlreadyInitialized = -9,
    };
    
    // ========================================
    // 구조체
    // ========================================
    
    // 완료 항목 (결과)
    struct CompletionEntry
    {
        RequestContext context;    // 요청 ID (사용자 정의)
        AsyncIOType type;          // 작업 타입
        int32_t result;            // 바이트 수 또는 에러 코드
        OSError osError;           // 시스템 에러 (0 = 성공)
        uint64_t completionTime;   // 완료 시간 (ns, 선택사항)
    };
    
    // 송수신 버퍼
    struct IOBuffer
    {
        void* data;                // 버퍼 포인터
        size_t size;               // 버퍼 크기
        size_t offset;             // 오프셋 (RIO BufferId 대신 사용 가능)
    };
    
    // 제공자 정보
    struct ProviderInfo
    {
        PlatformType platformType;
        const char* name;          // "IOCP", "RIO", "io_uring" 등
        uint32_t capabilities;     // 플래그 (지원 기능)
        size_t maxQueueDepth;      // 최대 큐 깊이
        size_t maxConcurrentReq;   // 최대 동시 요청
        bool supportsBufferReg;    // 버퍼 사전 등록 지원
        bool supportsBatching;     // 배치 처리 지원
        bool supportsZeroCopy;     // Zero-copy 지원
    };
    
    // 통계 정보
    struct ProviderStats
    {
        uint64_t totalRequests;    // 전체 요청 수
        uint64_t totalCompletions; // 전체 완료 수
        uint64_t pendingRequests;  // 대기 중인 요청 수
        uint64_t avgLatency;       // 평균 레이턴시 (ns)
        double p99Latency;         // P99 레이턴시
        uint64_t errorCount;       // 에러 수
    };
    
    // ========================================
    // 추상 기본 클래스
    // ========================================
    
    class AsyncIOProvider
    {
    public:
        virtual ~AsyncIOProvider() = default;
        
        // ========================================
        // 생명주기 관리
        // ========================================
        
        /**
         * 초기화
         * @param queueDepth: 요청/완료 큐 깊이 (32-4096)
         * @param maxConcurrent: 최대 동시 요청 수
         * @return 성공 여부
         */
        virtual AsyncIOError Initialize(size_t queueDepth, size_t maxConcurrent) = 0;
        
        /**
         * 정리
         */
        virtual void Shutdown() = 0;
        
        /**
         * 초기화 여부 확인
         */
        virtual bool IsInitialized() const = 0;
        
        // ========================================
        // 버퍼 관리
        // ========================================
        
        /**
         * 버퍼 사전 등록 (선택사항, 성능 향상)
         * @param ptr: 버퍼 포인터
         * @param size: 버퍼 크기
         * @return 버퍼 ID (0 이상 = 성공, 음수 = 에러)
         * 
         * RIO/io_uring에서만 의미 있음 (IOCP는 no-op)
         */
        virtual int64_t RegisterBuffer(const void* ptr, size_t size) = 0;
        
        /**
         * 버퍼 등록 해제
         * @param bufferId: RegisterBuffer에서 반환한 ID
         */
        virtual AsyncIOError UnregisterBuffer(int64_t bufferId) = 0;
        
        // ========================================
        // 비동기 I/O 요청
        // ========================================
        
        /**
         * 송신 요청
         * @param socket: 소켓
         * @param buffer: 전송 버퍼
         * @param size: 전송 크기
         * @param context: 요청 ID (완료 시 반환됨)
         * @param flags: 플래그 (RIO_MSG_DEFER 등, 플랫폼 무관)
         * @return 성공 여부
         * 
         * 주의: 플랫폼마다 지연 시간이 다름
         * - IOCP: 즉시 실행 (flags 무시)
         * - RIO: RIO_MSG_DEFER 시 배치 처리 대기
         * - io_uring: 자동 배치 처리
         */
        virtual AsyncIOError SendAsync(
            SocketHandle socket,
            const void* buffer,
            size_t size,
            RequestContext context,
            uint32_t flags = 0
        ) = 0;
        
        /**
         * 수신 요청
         * @param socket: 소켓
         * @param buffer: 수신 버퍼
         * @param size: 버퍼 크기
         * @param context: 요청 ID
         * @param flags: 플래그
         * @return 성공 여부
         */
        virtual AsyncIOError RecvAsync(
            SocketHandle socket,
            void* buffer,
            size_t size,
            RequestContext context,
            uint32_t flags = 0
        ) = 0;
        
        /**
         * 배치 실행 (선택사항)
         * 
         * IOCP: no-op (SendAsync가 즉시 실행)
         * RIO: RIO_MSG_DEFER 플래그로 대기 중인 요청 커널 전달
         * io_uring: SQ에 있는 요청 모두 커널 전달
         */
        virtual AsyncIOError FlushRequests() = 0;
        
        // ========================================
        // 완료 처리
        // ========================================
        
        /**
         * 완료된 작업 처리 (Non-blocking)
         * @param entries: 완료 항목 배열 (출력)
         * @param maxEntries: 배열 크기
         * @param timeoutMs: 타임아웃
         *     - 0: Non-blocking (즉시 반환)
         *     - >0: 밀리초 단위 대기
         *     - -1: 무한 대기
         * @return 처리된 완료 개수 (음수 = 에러)
         * 
         * 예시:
         *   CompletionEntry entries[32];
         *   int count = ProcessCompletions(entries, 32, 1000);
         *   if (count > 0) {
         *       for (int i = 0; i < count; i++) {
         *           HandleCompletion(entries[i]);
         *       }
         *   }
         */
        virtual int ProcessCompletions(
            CompletionEntry* entries,
            size_t maxEntries,
            int timeoutMs = 0
        ) = 0;
        
        // ========================================
        // 정보 조회
        // ========================================
        
        /**
         * 제공자 정보 조회
         */
        virtual const ProviderInfo& GetInfo() const = 0;
        
        /**
         * 통계 조회
         */
        virtual ProviderStats GetStats() const = 0;
        
        /**
         * 마지막 에러 메시지
         */
        virtual const char* GetLastError() const = 0;
    };
    
    // ========================================
    // 팩토리 함수
    // ========================================
    
    /**
     * AsyncIOProvider 생성 (플랫폼 자동 선택)
     * - Windows 8+: RIO
     * - Windows 7-: IOCP
     * - Linux 5.1+: io_uring
     * - Linux 4.x: epoll
     * - macOS: kqueue (미래)
     * 
     * @return 플랫폼에 맞는 provider 또는 nullptr
     */
    std::unique_ptr<AsyncIOProvider> CreateAsyncIOProvider();
    
    /**
     * AsyncIOProvider 생성 (명시적 선택)
     * @param platformHint: "IOCP", "RIO", "io_uring", "epoll" 등
     * @return 지정된 provider 또는 nullptr (지원 안 함)
     */
    std::unique_ptr<AsyncIOProvider> CreateAsyncIOProvider(
        const char* platformHint
    );
    
    /**
     * 플랫폼 지원 여부 확인
     * @param platformHint: 플랫폼명
     * @return 지원 여부
     */
    bool IsPlatformSupported(const char* platformHint);
    
    /**
     * 지원하는 모든 플랫폼 목록
     * @return 플랫폼명 배열
     */
    const char** GetSupportedPlatforms(size_t& outCount);
}
```

---

## Windows 구현 (RIO)

### RIOAsyncIOProvider.h

```cpp
#pragma once

#include "../AsyncIOProvider.h"
#include <winsock2.h>
#include <mswsock.h>
#include <vector>
#include <queue>
#include <map>

namespace RAON::Network::AsyncIO::Windows
{
    // ========================================
    // RIO 함수 포인터
    // ========================================
    
    struct RIOFunctions
    {
        decltype(&RIOInitialize)* Initialize;
        decltype(&RIOCreateCompletionQueue)* CreateCQ;
        decltype(&RIOCreateRequestQueue)* CreateRQ;
        decltype(&RIOCloseCompletionQueue)* CloseCQ;
        decltype(&RIOCloseRequestQueue)* CloseRQ;
        decltype(&RIOSend)* Send;
        decltype(&RIORecv)* Recv;
        decltype(&RIOCommitSends)* CommitSends;
        decltype(&RIOCommitRecvs)* CommitRecvs;
        decltype(&RIONotify)* Notify;
        decltype(&RIODequeueCompletion)* DequeueCompletion;
        decltype(&RIORegisterBuffer)* RegisterBuffer;
        decltype(&RIODeregisterBuffer)* DeregisterBuffer;
        
        bool Load();  // 동적 로드
    };
    
    // ========================================
    // RIO AsyncIOProvider 구현
    // ========================================
    
    class RIOAsyncIOProvider : public AsyncIOProvider
    {
    private:
        RIO_HANDLE mCQ;             // Completion Queue
        RIO_HANDLE mRQ;             // Request Queue (현재는 단일, 다중 지원 가능)
        
        // 버퍼 등록 추적
        struct RegisteredBuffer
        {
            RIO_BUFFERID id;
            void* ptr;
            size_t size;
        };
        std::map<int64_t, RegisteredBuffer> mBufferRegistry;
        int64_t mNextBufferId = 1;
        
        // 요청 추적
        struct PendingRequest
        {
            RequestContext context;
            RIO_BUF rioBuf;
            AsyncIOType type;
        };
        std::vector<PendingRequest> mPendingRequests;
        
        // 통계
        ProviderStats mStats = {};
        ProviderInfo mInfo = {};
        std::string mLastError;
        
        // 플랫폼 정보
        static RIOFunctions sRIOFuncs;
        static bool sRIOInitialized;
        
        bool IsRIOAvailable();
        
    public:
        RIOAsyncIOProvider();
        ~RIOAsyncIOProvider();
        
        AsyncIOError Initialize(size_t queueDepth, size_t maxConcurrent) override;
        void Shutdown() override;
        bool IsInitialized() const override;
        
        int64_t RegisterBuffer(const void* ptr, size_t size) override;
        AsyncIOError UnregisterBuffer(int64_t bufferId) override;
        
        AsyncIOError SendAsync(SocketHandle socket, const void* buffer,
                              size_t size, RequestContext context,
                              uint32_t flags) override;
        AsyncIOError RecvAsync(SocketHandle socket, void* buffer,
                              size_t size, RequestContext context,
                              uint32_t flags) override;
        AsyncIOError FlushRequests() override;
        
        int ProcessCompletions(CompletionEntry* entries, size_t maxEntries,
                              int timeoutMs) override;
        
        const ProviderInfo& GetInfo() const override;
        ProviderStats GetStats() const override;
        const char* GetLastError() const override;
        
    private:
        // 헬퍼 함수
        void LogError(const char* format, ...);
        AsyncIOError ConvertRIOResult(int rioResult);
    };
}
```

### RIOAsyncIOProvider.cpp 주요 부분

```cpp
namespace RAON::Network::AsyncIO::Windows
{
    RIOFunctions RIOAsyncIOProvider::sRIOFuncs = {};
    bool RIOAsyncIOProvider::sRIOInitialized = false;
    
    bool RIOFunctions::Load()
    {
        HMODULE ws2_32 = GetModuleHandleA("ws2_32.dll");
        if (!ws2_32) return false;
        
        // 함수 포인터 로드
        Initialize = (decltype(Initialize))GetProcAddress(ws2_32, "RIOInitialize");
        CreateCQ = (decltype(CreateCQ))GetProcAddress(ws2_32, "RIOCreateCompletionQueue");
        // ... 나머지 함수들
        
        return Initialize && CreateCQ && CreateRQ;  // 필수 함수 확인
    }
    
    AsyncIOError RIOAsyncIOProvider::Initialize(size_t queueDepth, size_t maxConcurrent)
    {
        if (!IsRIOAvailable())
            return AsyncIOError::PlatformNotSupported;
        
        if (mCQ != NULL)
            return AsyncIOError::AlreadyInitialized;
        
        // RIO 초기화
        RIO_NOTIFICATION_COMPLETION notification;
        notification.Type = RIO_IOCP_COMPLETION;
        notification.Iocp.IocpHandle = NULL;  // 내부 IOCP 생성
        notification.Iocp.CompletionKey = (ULONG_PTR)this;
        notification.Iocp.Flags = 0;
        
        // CQ 생성
        mCQ = sRIOFuncs.CreateCQ(queueDepth, &notification);
        if (mCQ == RIO_INVALID_CQ)
            return LogError("RIOCreateCompletionQueue failed");
        
        // RQ 생성 (단일 소켓용 - 나중에 다중 지원)
        mRQ = sRIOFuncs.CreateRQ(INVALID_SOCKET, 
                                 maxConcurrent,  // maxRecvs
                                 maxConcurrent,  // maxSends
                                 mCQ, NULL);
        if (mRQ == RIO_INVALID_RQ)
        {
            sRIOFuncs.CloseCQ(mCQ);
            return LogError("RIOCreateRequestQueue failed");
        }
        
        mInfo.platformType = PlatformType::RIO;
        mInfo.name = "RIO";
        mInfo.maxQueueDepth = queueDepth;
        mInfo.maxConcurrentReq = maxConcurrent;
        mInfo.supportsBufferReg = true;
        mInfo.supportsBatching = true;
        mInfo.supportsZeroCopy = true;
        
        return AsyncIOError::Success;
    }
    
    AsyncIOError RIOAsyncIOProvider::SendAsync(
        SocketHandle socket,
        const void* buffer,
        size_t size,
        RequestContext context,
        uint32_t flags)
    {
        if (mRQ == RIO_INVALID_RQ)
            return AsyncIOError::NotInitialized;
        
        // 버퍼를 RIO_BUF로 변환
        RIO_BUF rioBuf;
        rioBuf.BufferId = RIO_INVALID_BUFFERID;  // 등록 안 함 (즉시 지정)
        rioBuf.Offset = 0;
        rioBuf.Length = size;
        
        // RIOSend (RIO_MSG_DEFER로 배치 처리 대기)
        if (!sRIOFuncs.Send(mRQ, &rioBuf, 1, flags | RIO_MSG_DEFER, context))
        {
            return LogError("RIOSend failed: %d", WSAGetLastError());
        }
        
        mStats.totalRequests++;
        mStats.pendingRequests++;
        
        return AsyncIOError::Success;
    }
    
    AsyncIOError RIOAsyncIOProvider::FlushRequests()
    {
        if (mRQ == RIO_INVALID_RQ)
            return AsyncIOError::NotInitialized;
        
        // 대기 중인 요청 배치 실행
        if (!sRIOFuncs.CommitSends(mRQ))
        {
            return LogError("RIOCommitSends failed: %d", WSAGetLastError());
        }
        
        return AsyncIOError::Success;
    }
    
    int RIOAsyncIOProvider::ProcessCompletions(
        CompletionEntry* entries,
        size_t maxEntries,
        int timeoutMs)
    {
        if (mCQ == RIO_INVALID_CQ)
            return (int)AsyncIOError::NotInitialized;
        
        std::vector<RIO_CQ_ENTRY> rioCQE(maxEntries);
        
        // RIODequeueCompletion은 non-blocking
        // timeoutMs 처리는 IOCP로 수행 (notification.Type == RIO_IOCP_COMPLETION)
        DWORD bytes;
        ULONG_PTR key;
        OVERLAPPED* overlapped;
        DWORD timeout = (timeoutMs < 0) ? INFINITE : timeoutMs;
        
        // IOCP로 완료 대기
        if (!GetQueuedCompletionStatus(notification.Iocp.IocpHandle,
                                      &bytes, &key, &overlapped, timeout))
        {
            if (GetLastError() == WAIT_TIMEOUT)
                return 0;
            return (int)AsyncIOError::OperationFailed;
        }
        
        // CQ에서 항목 추출
        ULONG cqCount = sRIOFuncs.DequeueCompletion(
            mCQ, 
            rioCQE.data(),
            (ULONG)maxEntries
        );
        
        if (cqCount == RIO_CORRUPT_CQ)
            return (int)AsyncIOError::OperationFailed;
        
        // 변환
        for (ULONG i = 0; i < cqCount; i++)
        {
            entries[i].context = (RequestContext)rioCQE[i].RequestContext;
            entries[i].type = AsyncIOType::Send;  // 추적 필요
            entries[i].result = (int32_t)rioCQE[i].BytesTransferred;
            entries[i].osError = 0;
        }
        
        mStats.totalCompletions += cqCount;
        mStats.pendingRequests -= cqCount;
        
        return (int)cqCount;
    }
}
```

---

## Linux 구현 (io_uring)

### IOUringAsyncIOProvider.h

```cpp
#pragma once

#include "../AsyncIOProvider.h"
#include <liburing.h>
#include <vector>
#include <map>
#include <queue>

namespace RAON::Network::AsyncIO::Linux
{
    class IOUringAsyncIOProvider : public AsyncIOProvider
    {
    private:
        struct io_uring mRing;
        bool mInitialized = false;
        
        // User data to context 맵
        struct UserDataEntry
        {
            RequestContext context;
            AsyncIOType type;
        };
        std::map<uint64_t, UserDataEntry> mUserDataMap;
        uint64_t mNextUserData = 1;
        
        // 버퍼 등록 추적
        struct RegisteredBuffer
        {
            void* ptr;
            size_t size;
            int bufferIndex;  // io_uring fixed buffer index
        };
        std::map<int64_t, RegisteredBuffer> mBufferRegistry;
        int64_t mNextBufferId = 1;
        
        // 통계
        ProviderStats mStats = {};
        ProviderInfo mInfo = {};
        std::string mLastError;
        
    public:
        IOUringAsyncIOProvider();
        ~IOUringAsyncIOProvider();
        
        AsyncIOError Initialize(size_t queueDepth, size_t maxConcurrent) override;
        void Shutdown() override;
        bool IsInitialized() const override;
        
        int64_t RegisterBuffer(const void* ptr, size_t size) override;
        AsyncIOError UnregisterBuffer(int64_t bufferId) override;
        
        AsyncIOError SendAsync(SocketHandle socket, const void* buffer,
                              size_t size, RequestContext context,
                              uint32_t flags) override;
        AsyncIOError RecvAsync(SocketHandle socket, void* buffer,
                              size_t size, RequestContext context,
                              uint32_t flags) override;
        AsyncIOError FlushRequests() override;
        
        int ProcessCompletions(CompletionEntry* entries, size_t maxEntries,
                              int timeoutMs) override;
        
        const ProviderInfo& GetInfo() const override;
        ProviderStats GetStats() const override;
        const char* GetLastError() const override;
        
    private:
        void LogError(const char* format, ...);
        AsyncIOError ConvertIOUringError(int error);
    };
}
```

### IOUringAsyncIOProvider.cpp 주요 부분

```cpp
namespace RAON::Network::AsyncIO::Linux
{
    AsyncIOError IOUringAsyncIOProvider::Initialize(
        size_t queueDepth,
        size_t maxConcurrent)
    {
        if (mInitialized)
            return AsyncIOError::AlreadyInitialized;
        
        // io_uring 초기화
        struct io_uring_params params = {};
        // IORING_SETUP_SINGLE_ISSUER: 단일 스레드에서만 submit 호출
        // IORING_SETUP_DEFER_TASKRUN: 적응형 작업 처리
        params.flags = IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN;
        
        int ret = io_uring_queue_init_params(
            queueDepth > 4096 ? 4096 : queueDepth,
            &mRing,
            &params
        );
        
        if (ret < 0)
            return LogError("io_uring_queue_init_params failed: %d", ret);
        
        mInitialized = true;
        
        mInfo.platformType = PlatformType::IOUring;
        mInfo.name = "io_uring";
        mInfo.maxQueueDepth = mRing.sq.ring_sz;
        mInfo.maxConcurrentReq = mRing.cq.ring_sz;
        mInfo.supportsBufferReg = true;
        mInfo.supportsBatching = true;
        mInfo.supportsZeroCopy = true;
        
        return AsyncIOError::Success;
    }
    
    AsyncIOError IOUringAsyncIOProvider::SendAsync(
        SocketHandle socket,
        const void* buffer,
        size_t size,
        RequestContext context,
        uint32_t flags)
    {
        if (!mInitialized)
            return AsyncIOError::NotInitialized;
        
        // SQ Entry 획득
        struct io_uring_sqe* sqe = io_uring_get_sqe(&mRing);
        if (!sqe)
        {
            // SQ가 가득 찬 경우
            io_uring_submit(&mRing);  // 강제 submit
            sqe = io_uring_get_sqe(&mRing);
            if (!sqe)
                return AsyncIOError::NoResources;
        }
        
        // 사용자 데이터 맵 생성
        uint64_t userData = mNextUserData++;
        mUserDataMap[userData] = {context, AsyncIOType::Send};
        
        // send 요청 준비
        io_uring_prep_send(sqe, socket, (void*)buffer, size, flags);
        sqe->user_data = userData;
        
        mStats.totalRequests++;
        mStats.pendingRequests++;
        
        return AsyncIOError::Success;
    }
    
    AsyncIOError IOUringAsyncIOProvider::FlushRequests()
    {
        if (!mInitialized)
            return AsyncIOError::NotInitialized;
        
        // SQ의 모든 요청을 커널로 전달
        int ret = io_uring_submit(&mRing);
        if (ret < 0)
            return LogError("io_uring_submit failed: %d", ret);
        
        return AsyncIOError::Success;
    }
    
    int IOUringAsyncIOProvider::ProcessCompletions(
        CompletionEntry* entries,
        size_t maxEntries,
        int timeoutMs)
    {
        if (!mInitialized)
            return (int)AsyncIOError::NotInitialized;
        
        struct __kernel_timespec ts;
        struct __kernel_timespec* pts = nullptr;
        
        if (timeoutMs > 0)
        {
            ts.tv_sec = timeoutMs / 1000;
            ts.tv_nsec = (timeoutMs % 1000) * 1000000;
            pts = &ts;
        }
        else if (timeoutMs < 0)
        {
            // 무한 대기는 timeoutMs=0으로 처리
        }
        
        // CQ 처리 (루프)
        struct io_uring_cqe* cqe;
        unsigned head;
        int count = 0;
        
        // 대기 (timeoutMs < 0일 때만 블로킹)
        if (timeoutMs != 0)
        {
            int ret = io_uring_wait_cqe_timeout(&mRing, &cqe, pts);
            if (ret == -ETIME)
                return 0;  // 타임아웃
            if (ret < 0)
                return (int)AsyncIOError::OperationFailed;
        }
        
        // CQ 항목 추출
        unsigned remaining = io_uring_cq_ready(&mRing);
        size_t toProcess = remaining < maxEntries ? remaining : maxEntries;
        
        io_uring_for_each_cqe(&mRing, head, cqe)
        {
            if (count >= (int)maxEntries)
                break;
            
            auto it = mUserDataMap.find(cqe->user_data);
            if (it != mUserDataMap.end())
            {
                entries[count].context = it->second.context;
                entries[count].type = it->second.type;
                entries[count].result = (int32_t)cqe->res;
                entries[count].osError = cqe->res < 0 ? -cqe->res : 0;
                
                mUserDataMap.erase(it);
                count++;
            }
        }
        
        io_uring_cq_advance(&mRing, count);
        
        mStats.totalCompletions += count;
        mStats.pendingRequests -= count;
        
        return count;
    }
}
```

---

## 호환성 계층 (Compatibility Layer)

기존 RAON IocpCore 코드와의 호환성을 유지하면서 새로운 AsyncIOProvider 인터페이스를 사용합니다.

### 문제 상황

기존 RAON 코드:
```cpp
// 기존: RAON IocpObjectSession
class IocpObjectSession : public ObjectSession
{
private:
    // 영文: IOCP completion callback
    // 한글: IOCP 완료 콜백
    void HandleIocp(LPOVERLAPPED overlapped, DWORD bytesTransferred, DWORD dwError)
    {
        // 영文: Handle result of async operation
        // 한글: 비동기 작업 결과 처리
        if (dwError == NO_ERROR && bytesTransferred > 0)
        {
            ProcessData(bytesTransferred);
        }
    }
};
```

새로운 AsyncIOProvider 콜백 인터페이스:
```cpp
// 새로: AsyncIOProvider 완료 콜백
using CompletionCallback = std::function<void(
    const CompletionResult& result,
    void* userContext
)>;
```

**불일치 요소**:
- 반환값: LPOVERLAPPED vs CompletionResult 구조체
- 파라미터 개수: 3개 vs 2개
- 에러 코드: Windows DWORD vs 플랫폼 독립적 ErrorCode enum

### 해결책: 어댑터 패턴

#### 1단계: 콜백 변환 어댑터

```cpp
// File: AsyncIO/Compatibility/IocpSessionAdapter.h
// 영문: Adapter to convert new AsyncIOProvider callbacks to old IOCP style
// 한글: 새로운 AsyncIOProvider 콜백을 기존 IOCP 스타일로 변환하는 어댑터

namespace RAON::Network::AsyncIO::Compatibility
{
    // 영文: Context wrapper for conversion between callback styles
    // 한글: 콜백 스타일 변환을 위한 컨텍스트 래퍼
    class IocpCallbackAdapter
    {
    public:
        // 영문: Original IOCP callback function pointer
        // 한글: 원본 IOCP 콜백 함수 포인터
        using IocpCallback = void(*)(
            void* session,
            LPOVERLAPPED overlapped,
            DWORD bytesTransferred,
            DWORD dwError
        );

        IocpCallbackAdapter(void* sessionPtr, IocpCallback originalCallback)
            : mSessionPtr(sessionPtr)
            , mOriginalCallback(originalCallback)
        {
        }

        // 영文: Adapter function - called by AsyncIOProvider
        // 한글: 어댑터 함수 - AsyncIOProvider에서 호출
        void OnAsyncCompletion(
            const AsyncIOProvider::CompletionResult& result,
            void* userContext
        ) noexcept
        {
            // 영文: Convert CompletionResult back to IOCP style
            // 한글: CompletionResult를 IOCP 스타일로 변환
            LPOVERLAPPED overlapped = static_cast<LPOVERLAPPED>(userContext);
            
            // 영文: Map error codes: Platform-independent -> Windows DWORD
            // 한글: 에러 코드 매핑: 플랫폼 독립 -> Windows DWORD
            DWORD dwError = ConvertErrorCode(result.mErrorCode, result.mStatus);
            DWORD bytesTransferred = result.mBytesTransferred;

            // 영文: Call original IOCP-style callback
            // 한글: 원본 IOCP 스타일 콜백 호출
            if (mOriginalCallback)
            {
                mOriginalCallback(
                    mSessionPtr,
                    overlapped,
                    bytesTransferred,
                    dwError
                );
            }
        }

    private:
        // 영文: Convert platform-independent error to Windows error code
        // 한글: 플랫폼 독립적 에러를 Windows 에러 코드로 변환
        static DWORD ConvertErrorCode(
            int32_t platformError,
            AsyncIOProvider::CompletionResult::Status status
        )
        {
            switch (status)
            {
                case AsyncIOProvider::CompletionResult::Status::Success:
                    return NO_ERROR;

                case AsyncIOProvider::CompletionResult::Status::Timeout:
                    return WSAETIMEDOUT;

                case AsyncIOProvider::CompletionResult::Status::Cancelled:
                    return WSA_OPERATION_ABORTED;

                case AsyncIOProvider::CompletionResult::Status::Error:
                    // 영文: Map custom error codes back to WSA errors
                    // 한글: 사용자 정의 에러 코드를 WSA 에러로 매핑
                    return MapErrorCode(platformError);

                default:
                    return WSAEINVAL;
            }
        }

        static DWORD MapErrorCode(int32_t platformError)
        {
            // 영文: Map AsyncIO error codes to Windows WSAERROR
            // 한글: AsyncIO 에러 코드를 Windows WSAERROR로 매핑
            switch (platformError)
            {
                case static_cast<int32_t>(AsyncIOProvider::ErrorCode::ConnectionRefused):
                    return WSAECONNREFUSED;

                case static_cast<int32_t>(AsyncIOProvider::ErrorCode::ConnectionReset):
                    return WSAECONNRESET;

                case static_cast<int32_t>(AsyncIOProvider::ErrorCode::ConnectionTimeout):
                    return WSAETIMEDOUT;

                case static_cast<int32_t>(AsyncIOProvider::ErrorCode::BufferTooSmall):
                    return WSAENOBUFS;

                case static_cast<int32_t>(AsyncIOProvider::ErrorCode::SocketNotRegistered):
                    return WSAEINVAL;

                default:
                    return platformError;  // Pass through if unknown
            }
        }

        void* mSessionPtr;
        IocpCallback mOriginalCallback;
    };
}
```

#### 2단계: IocpObjectSession 호환성 래퍼

```cpp
// File: Network/IOCP/IocpObjectSession.h
// 영문: Modified IocpObjectSession to use AsyncIOProvider
// 한글: AsyncIOProvider를 사용하도록 수정된 IocpObjectSession

namespace RAON::Network
{
    class IocpObjectSession : public ObjectSession
    {
    private:
        // 영문: Reference to unified async provider
        // 한글: 통일된 비동기 공급자 참조
        AsyncIOProvider* mAsyncProvider;

        // 영문: Compatibility adapter for callbacks
        // 한글: 콜백 호환성 어댑터
        std::unique_ptr<IocpCallbackAdapter> mCallbackAdapter;

        // 영文: Original IOCP handler
        // 한글: 원본 IOCP 핸들러
        void HandleIocpOriginal(
            LPOVERLAPPED overlapped,
            DWORD bytesTransferred,
            DWORD dwError
        );

    public:
        // 영文: Initialize with AsyncIOProvider
        // 한글: AsyncIOProvider로 초기화
        bool Initialize(AsyncIOProvider* provider)
        {
            mAsyncProvider = provider;
            mCallbackAdapter = std::make_unique<IocpCallbackAdapter>(
                this,
                [](void* session, LPOVERLAPPED overlapped, DWORD bytes, DWORD err)
                {
                    static_cast<IocpObjectSession*>(session)->HandleIocpOriginal(
                        overlapped, bytes, err
                    );
                }
            );
            return true;
        }

        // 영文: Send using AsyncIOProvider with adapter callback
        // 한글: 어댑터 콜백과 함께 AsyncIOProvider를 사용하여 송신
        AsyncIOError SendData(const void* buffer, size_t length)
        {
            // 영문: Create OVERLAPPED structure as user context
            // 한글: 사용자 컨텍스트로 OVERLAPPED 구조체 생성
            LPOVERLAPPED overlapped = CreateOverlapped();

            // 영文: Create callback that invokes adapter
            // 한글: 어댑터를 호출하는 콜백 생성
            auto asyncCallback = [this, overlapped](
                const AsyncIOProvider::CompletionResult& result,
                void* context
            ) noexcept
            {
                mCallbackAdapter->OnAsyncCompletion(result, overlapped);
            };

            // 영文: Send using new AsyncIOProvider
            // 한글: 새로운 AsyncIOProvider를 사용하여 송신
            return mAsyncProvider->SendAsync(
                mSocket,
                buffer,
                length,
                asyncCallback
            );
        }
    };
}
```

### 3단계: 마이그레이션 경로

**Phase 1: 호환성 모드 (기존 코드 유지)**
```cpp
// 영文: Existing code works as-is with compatibility adapter
// 한글: 호환성 어댑터를 사용하여 기존 코드 그대로 작동

// 기존 코드 변경 없음
auto session = std::make_unique<IocpObjectSession>();
session->Initialize(asyncProvider);
session->SendData(buffer, length);
```

**Phase 2: 점진적 전환 (신규 코드부터 새 패턴 사용)**
```cpp
// 영文: New code uses AsyncIOProvider directly
// 한글: 새 코드는 AsyncIOProvider를 직접 사용

auto callback = [](const CompletionResult& result, void* ctx) noexcept
{
    if (result.mStatus == CompletionResult::Status::Success)
    {
        // 영文: Handle success
        // 한글: 성공 처리
    }
};

asyncProvider->SendAsync(socket, buffer, length, callback);
```

**Phase 3: 완전 전환 (모든 코드 마이그레이션 완료)**
```cpp
// 영文: All code uses modern AsyncIOProvider pattern
// 한글: 모든 코드가 현대식 AsyncIOProvider 패턴 사용

// IocpObjectSession 제거 또는 AsyncObjectSession 사용
auto session = std::make_unique<AsyncObjectSession>();
```

### 호환성 체크리스트

- [ ] CompletionResult 구조체 정의 완료
- [ ] ErrorCode enum 및 변환 함수 구현
- [ ] IocpCallbackAdapter 클래스 구현
- [ ] IocpObjectSession 호환성 레이어 추가
- [ ] 기존 RAON 코드 미변경 상태에서 테스트 완료
- [ ] 마이그레이션 경로 문서화 완료

---

## 플랫폼 선택 전략

### 런타임 감지 및 선택

```cpp
// File: AsyncIO/Platform/PlatformDetect.h

#ifdef _WIN32
    // Windows 버전 감지
    DWORD major, minor;
    GetOSVersion(major, minor);
    
    if (major > 6 || (major == 6 && minor >= 2))  // Windows 8+
    {
        provider = std::make_unique<RIOAsyncIOProvider>();
    }
    else
    {
        provider = std::make_unique<IocpAsyncIOProvider>();
    }
#else
    // Linux 커널 버전 감지
    struct utsname buf;
    uname(&buf);
    
    // io_uring 지원 확인
    if (io_uring_setup_probe() != nullptr)  // 또는 io_uring_queue_init 시도
    {
        provider = std::make_unique<IOUringAsyncIOProvider>();
    }
    else
    {
        provider = std::make_unique<EpollAsyncIOProvider>();
    }
#endif
```

### 명시적 플랫폼 선택

```cpp
std::unique_ptr<AsyncIOProvider> CreateAsyncIOProvider(
    const char* platformHint)
{
    std::string hint = platformHint ? platformHint : "";
    
    #ifdef _WIN32
        if (hint == "RIO" || hint.empty())
        {
            auto provider = std::make_unique<RIOAsyncIOProvider>();
            if (provider->Initialize(...) == AsyncIOError::Success)
                return provider;
        }
        return std::make_unique<IocpAsyncIOProvider>();
    #else
        if (hint == "io_uring" || hint.empty())
        {
            auto provider = std::make_unique<IOUringAsyncIOProvider>();
            if (provider->Initialize(...) == AsyncIOError::Success)
                return provider;
        }
        return std::make_unique<EpollAsyncIOProvider>();
    #endif
}
```

---

## 에러 처리 전략

### 에러 반환 코드

```cpp
// AsyncIOError enum
Success = 0,
NotInitialized = -1,          // provider 미초기화
InvalidSocket = -2,           // 잘못된 소켓
OperationPending = -3,        // 작업 대기 중
OperationFailed = -4,         // 작업 실패
InvalidBuffer = -5,           // 잘못된 버퍼
NoResources = -6,             // 리소스 부족
Timeout = -7,                 // 타임아웃
PlatformNotSupported = -8,    // 플랫폼 미지원
AlreadyInitialized = -9,      // 이미 초기화됨
```

### 에러 복구 전략

```cpp
// IocpCore에서의 사용 예시

AsyncIOError result = mAsyncProvider->SendAsync(...);
if (result != AsyncIOError::Success)
{
    switch (result)
    {
    case AsyncIOError::NotInitialized:
        LOG_ERROR("AsyncIO not initialized");
        return false;
        
    case AsyncIOError::NoResources:
        // SQ/CQ가 가득 찬 경우
        LOG_WARNING("Resource exhausted, flushing...");
        mAsyncProvider->FlushRequests();
        // 재시도는 상위 계층에서
        return false;
        
    case AsyncIOError::InvalidSocket:
        LOG_ERROR("Invalid socket");
        return false;
        
    default:
        LOG_ERROR("Async I/O error: %d", (int)result);
        return false;
    }
}
```

---

## 메모리 관리 전략

### RIO 버퍼 풀

```cpp
// File: AsyncIO/Windows/RIOBufferPool.h

class RIOBufferPool
{
private:
    struct Buffer
    {
        RIO_BUFFERID id;
        void* ptr;
        size_t size;
        bool inUse;
    };
    
    std::vector<Buffer> mPool;
    RIO_HANDLE mCQ;
    
public:
    bool Initialize(RIO_HANDLE cq, size_t bufferSize, size_t poolSize)
    {
        mCQ = cq;
        mPool.resize(poolSize);
        
        for (size_t i = 0; i < poolSize; i++)
        {
            mPool[i].ptr = malloc(bufferSize);
            if (!mPool[i].ptr) return false;
            
            mPool[i].id = RIORegisterBuffer(mPool[i].ptr, bufferSize);
            if (mPool[i].id == RIO_INVALID_BUFFERID)
            {
                free(mPool[i].ptr);
                return false;
            }
            
            mPool[i].size = bufferSize;
            mPool[i].inUse = false;
        }
        
        return true;
    }
    
    RIO_BUFFERID Acquire(size_t size, void*& outPtr)
    {
        for (auto& buf : mPool)
        {
            if (!buf.inUse && buf.size >= size)
            {
                buf.inUse = true;
                outPtr = buf.ptr;
                return buf.id;
            }
        }
        return RIO_INVALID_BUFFERID;  // 풀 고갈
    }
    
    void Release(RIO_BUFFERID id)
    {
        for (auto& buf : mPool)
        {
            if (buf.id == id)
            {
                buf.inUse = false;
                return;
            }
        }
    }
};
```

### io_uring Fixed Buffer Strategy

io_uring의 고정 버퍼 기능은 성능 최적화를 위해 매우 중요합니다.

#### Overview: Fixed Buffer Registration

```cpp
// 영문: io_uring fixed buffer registration overview
// 한글: io_uring 고정 버퍼 등록 개요

// Traditional approach (dynamic buffers):
// - Each operation specifies buffer pointer
// - Kernel validates buffer permissions on each operation
// - High overhead for frequent operations
struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
io_uring_prep_read(sqe, fd, buffer_ptr, buffer_size, offset);

// Fixed buffer approach (pre-registered):
// - Register buffers upfront with io_uring_register_buffers()
// - Operations only reference buffer index + offset
// - No per-operation validation needed
// - ~20-30% performance improvement for small messages
struct iovec iov[NUM_BUFFERS];
for (int i = 0; i < NUM_BUFFERS; i++) {
    iov[i].iov_base = buffer_pool[i];
    iov[i].iov_len = BUFFER_SIZE;
}
io_uring_register_buffers(&ring, iov, NUM_BUFFERS);

struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
io_uring_prep_read_fixed(sqe, fd, buffer_ptr, length, offset, buffer_index);
```

#### Implementation Pattern

```cpp
// File: AsyncIO/Linux/IOUringBufferPool.h
// 영문: Efficient buffer pool for io_uring fixed buffers
// 한글: io_uring 고정 버퍼용 효율적 버퍼 풀

class IOUringBufferPool
{
private:
    static const uint32_t NUM_FIXED_BUFFERS = 256;
    static const uint32_t BUFFER_SIZE = 65536;

    // 영文: Buffer metadata
    // 한글: 버퍼 메타데이터
    struct FixedBuffer
    {
        uint8_t* mData;
        uint32_t mIndex;      // Index in io_uring registration
        std::atomic<bool> mInUse{false};
        CompletionCallback mCallback;
        void* mUserContext;
    };

    std::vector<FixedBuffer> mBuffers;
    std::queue<uint32_t> mFreeIndices;
    struct io_uring* mRing;
    std::mutex mLock;

public:
    IOUringBufferPool(struct io_uring* ring)
        : mRing(ring)
    {
        // 영文: Allocate all buffers contiguously
        // 한글: 모든 버퍼를 연속적으로 할당
        mBuffers.resize(NUM_FIXED_BUFFERS);
        
        std::vector<struct iovec> iov(NUM_FIXED_BUFFERS);
        
        for (uint32_t i = 0; i < NUM_FIXED_BUFFERS; ++i)
        {
            // 영文: Allocate aligned buffer for DMA
            // 한글: DMA용 정렬된 버퍼 할당
            mBuffers[i].mData = static_cast<uint8_t*>(
                aligned_alloc(4096, BUFFER_SIZE)
            );
            
            if (!mBuffers[i].mData)
                throw std::runtime_error("Buffer allocation failed");

            mBuffers[i].mIndex = i;
            iov[i].iov_base = mBuffers[i].mData;
            iov[i].iov_len = BUFFER_SIZE;
            mFreeIndices.push(i);
        }

        // 영文: Register all buffers with kernel
        // 한글: 모든 버퍼를 커널에 등록
        int ret = io_uring_register_buffers(mRing, iov.data(), NUM_FIXED_BUFFERS);
        if (ret < 0)
        {
            throw std::runtime_error(
                "io_uring_register_buffers failed: " + std::string(strerror(-ret))
            );
        }
    }

    ~IOUringBufferPool()
    {
        // 영文: Unregister buffers
        // 한글: 버퍼 등록 해제
        io_uring_unregister_buffers(mRing);

        // 영文: Free all buffers
        // 한글: 모든 버퍼 해제
        for (auto& buf : mBuffers)
        {
            if (buf.mData)
            {
                free(buf.mData);
                buf.mData = nullptr;
            }
        }
    }

    // 영문: Acquire a buffer from pool
    // 한글: 풀에서 버퍼 획득
    struct AcquireResult
    {
        uint32_t mBufferIndex = UINT32_MAX;  // Index for io_uring
        uint8_t* mData = nullptr;             // Pointer to buffer
        
        bool IsValid() const { return mBufferIndex != UINT32_MAX; }
    };

    AcquireResult AcquireBuffer()
    {
        std::lock_guard<std::mutex> lock(mLock);

        if (mFreeIndices.empty())
        {
            return AcquireResult();  // No buffers available
        }

        uint32_t idx = mFreeIndices.front();
        mFreeIndices.pop();

        mBuffers[idx].mInUse = true;

        return AcquireResult
        {
            .mBufferIndex = idx,
            .mData = mBuffers[idx].mData
        };
    }

    // 영문: Release buffer back to pool
    // 한글: 버퍼를 풀로 반환
    void ReleaseBuffer(uint32_t bufferIndex)
    {
        if (bufferIndex >= NUM_FIXED_BUFFERS)
            return;

        std::lock_guard<std::mutex> lock(mLock);

        mBuffers[bufferIndex].mInUse = false;
        mBuffers[bufferIndex].mCallback = nullptr;
        mBuffers[bufferIndex].mUserContext = nullptr;

        mFreeIndices.push(bufferIndex);
    }

    // 영문: Store callback and context for later retrieval
    // 한글: 나중 검색을 위해 콜백과 컨텍스트 저장
    void SetBufferCallback(
        uint32_t bufferIndex,
        CompletionCallback callback,
        void* userContext
    )
    {
        if (bufferIndex < NUM_FIXED_BUFFERS)
        {
            mBuffers[bufferIndex].mCallback = callback;
            mBuffers[bufferIndex].mUserContext = userContext;
        }
    }

    // 영文: Get callback for buffer
    // 한글: 버퍼에 대한 콜백 가져오기
    CompletionCallback GetBufferCallback(uint32_t bufferIndex)
    {
        if (bufferIndex < NUM_FIXED_BUFFERS)
            return mBuffers[bufferIndex].mCallback;
        return nullptr;
    }

    void* GetBufferContext(uint32_t bufferIndex)
    {
        if (bufferIndex < NUM_FIXED_BUFFERS)
            return mBuffers[bufferIndex].mUserContext;
        return nullptr;
    }

    uint32_t GetTotalBuffers() const
    {
        return NUM_FIXED_BUFFERS;
    }

    uint32_t GetAvailableBuffers() const
    {
        std::lock_guard<std::mutex> lock(mLock);
        return mFreeIndices.size();
    }
};
```

#### Integration with IOUringAsyncIOProvider

```cpp
// File: AsyncIO/Linux/IOUringAsyncIOProvider.h
// 영문: io_uring provider using fixed buffers
// 한글: 고정 버퍼를 사용하는 io_uring 공급자

class IOUringAsyncIOProvider : public AsyncIOProvider
{
private:
    struct io_uring mRing;
    std::unique_ptr<IOUringBufferPool> mBufferPool;

public:
    AsyncIOError Initialize(size_t queueDepth, size_t maxConcurrent) override
    {
        // 영文: Initialize io_uring ring
        // 한글: io_uring 링 초기화
        struct io_uring_params params = {};
        
        // 영文: Enable fixed files feature (if available)
        // 한글: 고정 파일 기능 활성화 (가능한 경우)
        params.flags |= IORING_SETUP_IOPOLL;  // Polling mode for lower latency

        int ret = io_uring_queue_init_params(queueDepth, &mRing, &params);
        if (ret < 0)
        {
            return AsyncIOError::InitializationFailed;
        }

        // 영文: Create fixed buffer pool
        // 한글: 고정 버퍼 풀 생성
        try
        {
            mBufferPool = std::make_unique<IOUringBufferPool>(&mRing);
        }
        catch (const std::exception& e)
        {
            io_uring_queue_exit(&mRing);
            return AsyncIOError::BufferAllocationFailed;
        }

        return AsyncIOError::Success;
    }

    // 영文: Send using fixed buffer
    // 한글: 고정 버퍼를 사용한 송신
    AsyncIOError SendAsyncFixed(
        SocketHandle socket,
        uint32_t bufferIndex,
        uint32_t length,
        CompletionCallback callback,
        void* userContext
    )
    {
        if (bufferIndex >= mBufferPool->GetTotalBuffers())
        {
            return AsyncIOError::InvalidBuffer;
        }

        // 영文: Get SQE (Submission Queue Entry)
        // 한글: SQE (제출 큐 항목) 가져오기
        struct io_uring_sqe* sqe = io_uring_get_sqe(&mRing);
        if (!sqe)
        {
            return AsyncIOError::QueueFull;
        }

        // 영文: Prepare fixed buffer write
        // 한글: 고정 버퍼 쓰기 준비
        io_uring_prep_write_fixed(
            sqe,
            socket,
            mBufferPool->GetBufferData(bufferIndex),
            length,
            0,  // offset
            bufferIndex
        );

        // 영文: Set user data for completion handling
        // 한글: 완료 처리를 위한 사용자 데이터 설정
        sqe->user_data = (uintptr_t)userContext;

        // 영文: Store callback in buffer pool
        // 한글: 버퍼 풀에 콜백 저장
        mBufferPool->SetBufferCallback(bufferIndex, callback, userContext);

        return AsyncIOError::Success;
    }

    // 영文: Process completions
    // 한글: 완료 처리
    int ProcessCompletions(
        CompletionEntry* entries,
        int maxEntries,
        int timeoutMs
    ) override
    {
        // 영문: Wait for completions
        // 한글: 완료 대기
        struct io_uring_cqe* cqe;
        unsigned head;
        int count = 0;

        struct __kernel_timespec ts = {};
        if (timeoutMs >= 0)
        {
            ts.tv_sec = timeoutMs / 1000;
            ts.tv_nsec = (timeoutMs % 1000) * 1000000;
        }

        // 영文: Get completions
        // 한글: 완료 취득
        int ret = io_uring_wait_cqe_timeout(&mRing, &cqe, &ts);
        if (ret == -ETIME)
        {
            return 0;  // Timeout
        }
        if (ret < 0)
        {
            return -1;  // Error
        }

        // 영文: Process all available completions
        // 한글: 사용 가능한 모든 완료 처리
        io_uring_for_each_cqe(&mRing, head, cqe)
        {
            if (count >= maxEntries)
                break;

            entries[count].mOperationId = cqe->user_data;
            entries[count].mResult = cqe->res;

            if (cqe->res >= 0)
            {
                entries[count].mStatus = CompletionStatus::Success;
                entries[count].mBytesTransferred = cqe->res;
            }
            else
            {
                entries[count].mStatus = CompletionStatus::Error;
                entries[count].mErrorCode = -cqe->res;
            }

            // 영문: Release buffer after completion
            // 한글: 완료 후 버퍼 해제
            uint32_t bufferIndex = (cqe->user_data >> 32) & 0xFFFFFFFF;
            mBufferPool->ReleaseBuffer(bufferIndex);

            count++;
        }

        io_uring_cq_advance(&mRing, count);

        return count;
    }
};
```

#### Performance Characteristics

| Aspect | Fixed Buffers | Dynamic Buffers |
|--------|---|---|
| **Setup Time** | 중간 (버퍼 등록) | 낮음 |
| **Per-Op Cost** | 매우 낮음 (인덱스만) | 높음 (주소 검증) |
| **Memory Usage** | 고정 (풀 크기) | 가변적 |
| **Best For** | 고처리량 (10K+ ops/sec) | 유연성 필요 시 |
| **Typical Gain** | +20-30% throughput | N/A |

#### When to Use Fixed Buffers

✅ **Use fixed buffers when**:
- 높은 처리량이 필요 (>10K ops/sec)
- 버퍼 크기가 일정함
- 메모리가 충분함
- 지연시간이 중요함

❌ **Don't use fixed buffers when**:
- 저처리량 애플리케이션
- 버퍼 크기가 가변적
- 메모리 제약이 있음
- 단순성이 더 중요함

---

## 성능 최적화 가이드

### 배치 크기 튜닝

```cpp
// RIO 예시
class RIOBatcher
{
private:
    static constexpr size_t BATCH_SIZE = 128;
    std::vector<PendingRequest> mBatch;
    
public:
    void AddRequest(const PendingRequest& req)
    {
        mBatch.push_back(req);
        if (mBatch.size() >= BATCH_SIZE)
            Flush();
    }
    
    void Flush()
    {
        // RIOCommitSends 한 번에 처리
        for (const auto& req : mBatch)
        {
            RIOSend(mRQ, &req.buf, 1, RIO_MSG_DEFER, ...);
        }
        RIOCommitSends(mRQ);
        mBatch.clear();
    }
};
```

### 타임아웃 전략

```cpp
// ProcessCompletions 호출 패턴
while (running)
{
    CompletionEntry entries[32];
    int count = mAsyncProvider->ProcessCompletions(
        entries,
        32,
        0  // Non-blocking (바로 반환)
    );
    
    if (count > 0)
    {
        for (int i = 0; i < count; i++)
            HandleCompletion(entries[i]);
    }
    
    // 정기적인 통계 리포트 (100ms마다)
    if (now - lastReport > 100ms)
    {
        auto stats = mAsyncProvider->GetStats();
        printf("Throughput: %.2f K ops/sec\n", 
               stats.totalCompletions / 100.0);
        lastReport = now;
    }
}
```

---

## 테스트 전략

### 단위 테스트 구조

```cpp
// File: AsyncIO/Test/AsyncIOTest.cpp

class AsyncIOProviderTest : public ::testing::Test
{
protected:
    std::unique_ptr<AsyncIOProvider> provider;
    
    void SetUp() override
    {
        provider = CreateAsyncIOProvider();
        ASSERT_NE(provider, nullptr);
        ASSERT_EQ(provider->Initialize(256, 100), AsyncIOError::Success);
    }
    
    void TearDown() override
    {
        provider->Shutdown();
    }
};

TEST_F(AsyncIOProviderTest, InitShutdown)
{
    // 이미 SetUp에서 테스트됨
    EXPECT_TRUE(provider->IsInitialized());
}

TEST_F(AsyncIOProviderTest, SendRecv)
{
    // 루프백 소켓
    auto [serverSock, clientSock] = CreateLoopbackPair();
    
    // 송신
    const char* data = "Hello";
    ASSERT_EQ(
        provider->SendAsync(clientSock, data, 5, 1, 0),
        AsyncIOError::Success
    );
    provider->FlushRequests();
    
    // 수신
    CompletionEntry entries[1];
    int count = provider->ProcessCompletions(entries, 1, 1000);
    ASSERT_EQ(count, 1);
    ASSERT_EQ(entries[0].result, 5);
    
    closesocket(serverSock);
    closesocket(clientSock);
}
```

### 성능 테스트 구조

```cpp
// File: AsyncIO/Benchmark/ThroughputBench.cpp

void BenchmarkSendThroughput()
{
    auto provider = CreateAsyncIOProvider();
    provider->Initialize(4096, 10000);
    
    auto [sock1, sock2] = CreateLoopbackPair();
    
    const size_t NUM_OPS = 1000000;
    const size_t BATCH_SIZE = 100;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 0; i < NUM_OPS; i++)
    {
        provider->SendAsync(sock1, data, size, i, 0);
        
        if ((i + 1) % BATCH_SIZE == 0)
            provider->FlushRequests();
    }
    
    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    double seconds = std::chrono::duration<double>(elapsed).count();
    double throughput = NUM_OPS / seconds;
    
    printf("Throughput: %.2f M ops/sec\n", throughput / 1e6);
    
    provider->Shutdown();
}
```

---

## 세션 매핑 전략 (Session Mapping Strategy)

RAON 네트워크 모듈의 핵심 문제: **SessionPool(인덱스 기반) vs AsyncIOProvider(void* context 기반) 매핑**

### 문제 분석

#### RAON 기존 구조: SessionPool
```cpp
// 영문: RAON existing SessionPool structure
// 한글: RAON 기존 SessionPool 구조

class SessionPool
{
private:
    // 영문: Array-based pool with indices 0~999
    // 한글: 인덱스 기반 풀 (0~999)
    std::array<std::unique_ptr<ObjectSession>, 1000> mSessions;
    
public:
    ObjectSession* GetSession(uint32_t sessionId)
    {
        // 영文: Direct array access
        // 한글: 직접 배열 접근
        if (sessionId < mSessions.size())
            return mSessions[sessionId].get();
        return nullptr;
    }
};

// 사용 예시
auto session = sessionPool.GetSession(5);  // 배열 인덱스로 접근
```

#### 새로운 AsyncIOProvider: void* Context
```cpp
// 영문: New AsyncIOProvider uses void* context for completions
// 한글: 새로운 AsyncIOProvider는 완료에 void* 컨텍스트 사용

struct CompletionEntry
{
    RequestContext context;    // ← 이것이 void*
    AsyncIOType type;
    int32_t result;
    OSError osError;
};

// ProcessCompletions 호출
int count = provider->ProcessCompletions(entries, maxCount, timeoutMs);
for (int i = 0; i < count; i++)
{
    // 영문: Need to map context back to ObjectSession
    // 한글: context를 ObjectSession으로 매핑해야 함
    void* context = entries[i].context;
    ObjectSession* session = ???;  // 어떻게 복원?
}
```

#### 핵심 문제

| 측면 | SessionPool | AsyncIOProvider |
|------|-------------|-----------------|
| **식별자 타입** | uint32_t (인덱스) | void* (포인터/임의) |
| **생명주기 관리** | Pool이 관리 | 사용자가 할당/해제 |
| **동시성 안전성** | Pool 인덱스 동기화 | Context 포인터 유효성 검증 필요 |
| **재사용 가능성** | 인덱스 재사용 가능 | 포인터 주소 재사용 위험 |
| **검증 방식** | `if (id < MAX) valid` | `if (ptr && IsValid(ptr)) valid` |

#### 잠재적 버그

```cpp
// 시나리오 1: UAF (Use-After-Free)
auto session = sessionPool.GetSession(5);
sessionPool.DestroySession(5);  // 메모리 해제

// 나중에 completions 처리
CompletionEntry entry;
entry.context = (void*)5;  // 이전 세션 ID
ObjectSession* s = (ObjectSession*)entry.context;  // 댕글링 포인터!
s->OnCompletion(...);  // CRASH

// 시나리오 2: Address Reuse
SessionPool pool;
auto session1 = new ObjectSession();
void* ctx1 = (void*)session1;
delete session1;

auto session2 = new ObjectSession();  // 같은 주소에 할당 가능
void* ctx2 = (void*)session2;

// ctx1과 ctx2가 동일하면, 완료가 잘못된 세션에 전달됨
```

### 솔루션: 3가지 옵션 비교

#### Option A: 인덱스 + 생성 번호 인코딩

```cpp
// 영문: Option A - Pack session ID and generation into uint64_t
// 한글: 옵션 A - 세션 ID와 생성 번호를 uint64_t에 패킹

class SessionContextEncoder
{
public:
    // 영문: Encode session ID and generation into context
    // 한글: 세션 ID와 생성 번호를 컨텍스트에 인코딩
    static RequestContext Encode(uint32_t sessionId, uint32_t generation)
    {
        // 상위 32비트: sessionId, 하위 32비트: generation
        uint64_t encoded = ((uint64_t)sessionId << 32) | (generation & 0xFFFFFFFF);
        return (RequestContext)encoded;
    }
    
    // 영문: Decode context back to session ID and generation
    // 한글: 컨텍스트를 세션 ID와 생성 번호로 디코드
    static std::pair<uint32_t, uint32_t> Decode(RequestContext context)
    {
        uint64_t encoded = (uint64_t)context;
        uint32_t sessionId = (uint32_t)(encoded >> 32);
        uint32_t generation = (uint32_t)(encoded & 0xFFFFFFFF);
        return {sessionId, generation};
    }
};

// 사용 예시
uint32_t sessionId = 5;
uint32_t generation = 3;
auto context = SessionContextEncoder::Encode(sessionId, generation);

// ProcessCompletions에서
for (int i = 0; i < count; i++)
{
    auto [id, gen] = SessionContextEncoder::Decode(entries[i].context);
    auto session = sessionPool.GetSession(id);
    if (session && session->GetGeneration() == gen)
    {
        session->OnCompletion(entries[i]);
    }
}
```

**장점**:
- ✅ 간단하고 빠름 (비트 연산)
- ✅ 추가 메모리 할당 없음
- ✅ 고유성 보장 (ID + 생성 번호)
- ✅ 기존 생성 번호 체계와 통합 가능

**단점**:
- ❌ 비트 폭 제한 (세션 ID 2^32, 생성 번호 2^32)
- ❌ 명확한 구조가 아님 (가독성 낮음)
- ❌ 확장 어려움 (추가 정보 인코딩 불가)

#### Option B: Context 구조체 (권장)

```cpp
// 영문: Option B - Context structure with session info
// 한글: 옵션 B - 세션 정보를 담는 컨텍스트 구조체

namespace RAON::Network::AsyncIO
{
    // 영문: Context wrapper for session tracking
    // 한글: 세션 추적을 위한 컨텍스트 래퍼
    struct SessionContext
    {
        // 영文: Session identifier (pool index)
        // 한글: 세션 식별자 (풀 인덱스)
        uint32_t sessionId;
        
        // 영文: Generation number for reuse validation
        // 한글: 재사용 검증을 위한 생성 번호
        uint32_t generation;
        
        // 영文: Actual session pointer (optional, for fast path)
        // 한글: 실제 세션 포인터 (선택사항, 빠른 경로용)
        void* sessionPtr;
        
        // 영문: Request metadata (operation type, flags)
        // 한글: 요청 메타데이터 (작업 타입, 플래그)
        uint16_t requestType;
        uint16_t flags;
    };
}

// 사용 예시
auto session = sessionPool.GetSession(5);
RequestContext context;

// 영文: Create context structure
// 한글: 컨텍스트 구조체 생성
auto ctx = new SessionContext{
    .sessionId = 5,
    .generation = session->GetGeneration(),
    .sessionPtr = session,  // 빠른 경로
    .requestType = ASYNCIO_REQUEST_SEND,
    .flags = 0
};
context = (RequestContext)ctx;

// ProcessCompletions에서
for (int i = 0; i < count; i++)
{
    auto ctx = static_cast<SessionContext*>(entries[i].context);
    
    // 영문: Validate generation (optional)
    // 한글: 생성 번호 검증 (선택사항)
    auto session = sessionPool.GetSession(ctx->sessionId);
    if (session && session->GetGeneration() == ctx->generation)
    {
        session->OnCompletion(entries[i]);
    }
    
    delete ctx;  // 정리
}
```

**장점**:
- ✅ 타입 안전성 (구조체)
- ✅ 확장 가능 (필드 추가 용이)
- ✅ 명확한 의도 (가독성 높음)
- ✅ 추가 정보 저장 가능 (requestType, flags 등)
- ✅ 디버깅 용이 (구조체 필드 검사)

**단점**:
- ❌ 메모리 할당 오버헤드 (new/delete)
- ❌ 캐시 프렌들리하지 않음 (포인터 역참조)
- ❌ 메모리 누수 위험 (delete 잊음)
- ❌ 동시성 문제 (동시 delete 안전성 필요)

#### Option C: 역매핑 테이블

```cpp
// 영문: Option C - Bidirectional mapping table
// 한글: 옵션 C - 양방향 매핑 테이블

class SessionContextMapper
{
private:
    // 영문: Context -> SessionId mapping
    // 한글: Context -> SessionId 매핑
    std::unordered_map<uintptr_t, uint32_t> mContextToSession;
    std::mutex mMutex;
    
public:
    // 영문: Register context for session
    // 한글: 세션을 위한 컨텍스트 등록
    RequestContext RegisterSession(uint32_t sessionId)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        
        // 영문: Use session ID as key directly
        // 한글: 세션 ID를 키로 직접 사용
        uintptr_t contextKey = sessionId;
        mContextToSession[contextKey] = sessionId;
        
        return (RequestContext)contextKey;
    }
    
    // 영문: Resolve context back to session
    // 한글: 컨텍스트를 세션으로 복원
    uint32_t ResolveSession(RequestContext context)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        
        auto it = mContextToSession.find((uintptr_t)context);
        if (it != mContextToSession.end())
            return it->second;
        
        return INVALID_SESSION_ID;
    }
    
    // 영문: Unregister context
    // 한글: 컨텍스트 등록 해제
    void UnregisterSession(RequestContext context)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mContextToSession.erase((uintptr_t)context);
    }
};

// 사용 예시
SessionContextMapper mapper;
auto context = mapper.RegisterSession(5);

// ProcessCompletions에서
for (int i = 0; i < count; i++)
{
    uint32_t sessionId = mapper.ResolveSession(entries[i].context);
    auto session = sessionPool.GetSession(sessionId);
    if (session)
    {
        session->OnCompletion(entries[i]);
        mapper.UnregisterSession(entries[i].context);
    }
}
```

**장점**:
- ✅ 유연함 (임의의 context 값 지원)
- ✅ 동시성 안전성 (뮤텍스 제공)
- ✅ 디버깅 용이 (매핑 상태 검사 가능)

**단점**:
- ❌ 락 오버헤드 (성능 저하)
- ❌ 메모리 오버헤드 (해시 맵)
- ❌ 메모리 누수 위험 (등록 해제 필수)
- ❌ 스케일링 성능 문제 (high contention)

### 권장 방안: Option B (수정된 버전)

**조건부 하이브리드 접근**:

```cpp
// 영문: Recommended hybrid approach - combine speed with safety
// 한글: 권장 하이브리드 접근 - 속도와 안전성 결합

namespace RAON::Network::AsyncIO
{
    // 영문: Session context for completion tracking
    // 한글: 완료 추적을 위한 세션 컨텍스트
    class SessionContextManager
    {
    private:
        // 영문: Pre-allocated context pool (RAII)
        // 한글: 사전 할당된 컨텍스트 풀 (RAII)
        struct ContextPool
        {
            static const uint32_t POOL_SIZE = 10000;
            std::array<SessionContext, POOL_SIZE> mContexts;
            std::queue<uint32_t> mFreeIndices;
            std::mutex mLock;
            
            ContextPool()
            {
                // 초기화
                for (uint32_t i = 0; i < POOL_SIZE; i++)
                    mFreeIndices.push(i);
            }
        };
        
        static ContextPool sPool;
        
    public:
        // 영문: Allocate context from pool (zero allocation after init)
        // 한글: 풀에서 컨텍스트 할당 (초기화 후 할당 없음)
        static RequestContext AllocateContext(
            uint32_t sessionId,
            uint32_t generation,
            void* sessionPtr
        )
        {
            std::lock_guard<std::mutex> lock(sPool.mLock);
            
            if (sPool.mFreeIndices.empty())
            {
                LOG_ERROR("Context pool exhausted!");
                return nullptr;
            }
            
            uint32_t idx = sPool.mFreeIndices.front();
            sPool.mFreeIndices.pop();
            
            auto& ctx = sPool.mContexts[idx];
            ctx.sessionId = sessionId;
            ctx.generation = generation;
            ctx.sessionPtr = sessionPtr;
            ctx.poolIndex = idx;  // 풀로 돌아갈 때 필요
            
            return (RequestContext)&ctx;
        }
        
        // 영문: Release context back to pool
        // 한글: 컨텍스트를 풀로 반환
        static void ReleaseContext(RequestContext context)
        {
            if (!context) return;
            
            auto ctx = static_cast<SessionContext*>(context);
            std::lock_guard<std::mutex> lock(sPool.mLock);
            sPool.mFreeIndices.push(ctx->poolIndex);
        }
        
        // 영문: Resolve and validate context
        // 한글: 컨텍스트 복원 및 검증
        static ObjectSession* ResolveSession(
            RequestContext context,
            SessionPool& pool
        )
        {
            if (!context) return nullptr;
            
            auto ctx = static_cast<SessionContext*>(context);
            
            // 영문: Validate by pointer first (fast path)
            // 한글: 포인터로 먼저 검증 (빠른 경로)
            if (ctx->sessionPtr)
            {
                auto session = static_cast<ObjectSession*>(ctx->sessionPtr);
                if (session && session->GetGeneration() == ctx->generation)
                    return session;  // Fast path hit
            }
            
            // 영문: Slow path: lookup by ID
            // 한글: 느린 경로: ID로 조회
            auto session = pool.GetSession(ctx->sessionId);
            if (session && session->GetGeneration() == ctx->generation)
                return session;
            
            return nullptr;  // Invalid or stale context
        }
    };
}
```

### 동시성 안전성 검증

#### 시나리오: 멀티 스레드 환경

```cpp
// 영문: Thread-safe session completion handling
// 한글: 스레드 안전 세션 완료 처리

class SafeSessionProcessor
{
private:
    SessionPool& mPool;
    SessionContextManager& mContextMgr;
    
public:
    void ProcessCompletions(
        AsyncIOProvider* provider,
        int timeoutMs
    )
    {
        std::array<CompletionEntry, 32> entries;
        int count = provider->ProcessCompletions(
            entries.data(),
            entries.size(),
            timeoutMs
        );
        
        for (int i = 0; i < count; i++)
        {
            // 영문: Resolve with validation
            // 한글: 검증과 함께 복원
            auto session = SessionContextManager::ResolveSession(
                entries[i].context,
                mPool
            );
            
            if (!session)
            {
                // 영문: Stale or invalid context - skip
                // 한글: 오래된 또는 잘못된 컨텍스트 - 건너뛰기
                LOG_WARNING("Invalid context in completion");
                SessionContextManager::ReleaseContext(entries[i].context);
                continue;
            }
            
            // 영문: Session exists and generation matches - safe to handle
            // 한글: 세션이 존재하고 생성 번호가 일치 - 안전하게 처리
            try
            {
                session->OnCompletion(entries[i]);
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("Exception in session completion: %s", e.what());
            }
            
            SessionContextManager::ReleaseContext(entries[i].context);
        }
    }
};
```

#### 스트레스 테스트: 높은 동시성

```cpp
// 영문: Stress test for high concurrency
// 한글: 높은 동시성을 위한 스트레스 테스트

void StressTest_HighConcurrency()
{
    SessionPool pool(1000);
    auto provider = CreateAsyncIOProvider();
    provider->Initialize(4096, 10000);
    SessionContextManager ctxMgr;
    
    // 영문: Create sender and receiver threads
    // 한글: 송신 및 수신 스레드 생성
    std::vector<std::thread> threads;
    std::atomic<uint64_t> totalOps(0);
    
    // 송신 스레드 (여러 개)
    for (int t = 0; t < 4; t++)
    {
        threads.emplace_back([&, t]()
        {
            for (int i = 0; i < 100000; i++)
            {
                uint32_t sessionId = (t * 25000 + i) % pool.GetCapacity();
                auto session = pool.GetSession(sessionId);
                if (!session) continue;
                
                auto ctx = ctxMgr.AllocateContext(
                    sessionId,
                    session->GetGeneration(),
                    session
                );
                
                provider->SendAsync(
                    session->GetSocket(),
                    "test",
                    4,
                    ctx,
                    0
                );
                
                totalOps++;
            }
        });
    }
    
    // 수신/완료 처리 스레드
    threads.emplace_back([&]()
    {
        SafeSessionProcessor processor(pool, ctxMgr);
        while (totalOps < 400000)
        {
            processor.ProcessCompletions(provider.get(), 100);
        }
    });
    
    // 세션 파괴 스레드 (의도적인 UAF 테스트)
    threads.emplace_back([&]()
    {
        for (int i = 0; i < 1000; i++)
        {
            pool.DestroySession(i);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });
    
    // 모든 스레드 대기
    for (auto& t : threads)
        t.join();
    
    LOG_INFO("Stress test completed: %llu operations", (unsigned long long)totalOps);
}
```

### 구현 체크리스트

- [ ] SessionContext 구조체 정의 (SessionContextManager.h)
- [ ] SessionContextManager 구현 (컨텍스트 풀, 할당/해제)
- [ ] SafeSessionProcessor 구현 (멀티스레드 안전)
- [ ] IocpCore 통합 (컨텍스트 생성 및 처리)
- [ ] 단위 테스트 작성 (정상/오류 경로)
- [ ] 스트레스 테스트 실행 (UAF, 동시성)
- [ ] 성능 벤치마크 (할당 오버헤드 측정)
- [ ] 문서 업데이트 (마이그레이션 가이드)

---

## 다음 단계

1. **AsyncIOProvider 인터페이스** 최종 확정
2. **RIOAsyncIOProvider** 구현 시작
3. **IocpCore** AsyncIOProvider 통합
4. **Linux io_uring** 구현
5. 크로스 플랫폼 **통합 테스트**

---

**참고**: 이 문서는 07_Performance_Analysis.md와 함께 완성됨.
