# API 설계 문서

**Version**: 1.0  
**Date**: 2026-01-27  
**Status**: Design Phase  
**Target Audience**: Library Users, Integration Engineers

---

## Table of Contents

1. [API Overview](#api-overview)
2. [Core Interfaces](#core-interfaces)
3. [Platform-Specific Implementations](#platform-specific-implementations)
4. [Usage Patterns](#usage-patterns)
5. [Error Handling](#error-handling)
6. [Advanced Features](#advanced-features)
7. [Deprecated/Legacy Support](#deprecatedlegacy-support)

---

## API Overview

### 목적 (Purpose)

The public API provides a unified, cross-platform interface for asynchronous I/O operations across Windows (IOCP/RIO), Linux (epoll/io_uring), and macOS (kqueue).

**Design Goals**:
- ✅ Single API for all platforms
- ✅ Automatic backend selection at runtime
- ✅ Zero-cost abstraction (performance parity with native implementations)
- ✅ Progressive enhancement (use best available backend)
- ✅ Backward compatibility (IOCP code works unchanged)

### 핵심 개념 (Core Concepts)

```
┌─────────────────────────────────────────────────────────┐
│                  AsyncIOProvider                        │
│            (Unified Abstract Interface)                 │
├─────────────────────────────────────────────────────────┤
│                                                          │
│ ┌──────────────┐  ┌──────────────┐  ┌──────────────┐   │
│ │IocpAsyncIO   │  │RIOAsyncIO    │  │IOUringAsync  │   │
│ │(Windows IOCP)│  │(Windows RIO) │  │(Linux)       │   │
│ └──────────────┘  └──────────────┘  └──────────────┘   │
│                                                          │
│ ┌──────────────┐  ┌──────────────┐                      │
│ │KqueueAsyncIO │  │EpollAsyncIO  │                      │
│ │(macOS)       │  │(Linux)       │                      │
│ └──────────────┘  └──────────────┘                      │
└─────────────────────────────────────────────────────────┘
```

---

## Core Interfaces

### 1. AsyncIOProvider (Abstract Base Class)

#### 목적
모든 플랫폼의 비동기 I/O 백엔드를 통일하는 인터페이스입니다.

#### 선언 (Declaration)

```cpp
// File: AsyncIOProvider.h
// 영문: Unified async I/O interface for all platforms
// 한글: 모든 플랫폼의 비동기 I/O를 통일하는 인터페이스

namespace Network
{
    // 영문: Completion callback function type
    // 한글: 완료 콜백 함수 타입
    using CompletionCallback = std::function<void(
        const CompletionResult& result,
        void* userContext
    )>;

    // 영문: Result of async operation
    // 한글: 비동기 작업의 결과
    struct CompletionResult
    {
        // 영문: Operation status (Success, Error)
        // 한글: 작업 상태 (성공, 오류)
        enum class Status
        {
            // 영문: Operation completed successfully
            // 한글: 작업이 성공적으로 완료됨
            Success = 0,

            // 영문: Operation failed with error code
            // 한글: 작업이 실패하고 오류 코드 포함
            Error = 1,

            // 영문: Operation cancelled by user
            // 한글: 사용자가 작업을 취소함
            Cancelled = 2,

            // 영문: Operation timed out
            // 한글: 작업이 타임아웃됨
            Timeout = 3,
        };

        // 영문: Operation status
        // 한글: 작업 상태
        Status mStatus;

        // 영문: Number of bytes transferred (for Send/Recv)
        // 한글: 전송된 바이트 수 (Send/Recv의 경우)
        uint32_t mBytesTransferred;

        // 영문: System error code (platform-specific)
        // 한글: 시스템 오류 코드 (플랫폼별)
        int32_t mErrorCode;

        // 영문: Operation type identifier
        // 한글: 작업 타입 식별자
        uint32_t mOperationId;

        // 영문: User context passed during operation registration
        // 한글: 작업 등록 시 전달된 사용자 컨텍스트
        void* mUserContext;
    };

    // 영문: Unified async I/O provider interface
    // 한글: 통일된 비동기 I/O 공급자 인터페이스
    class AsyncIOProvider
    {
    public:
        // 영문: Virtual destructor
        // 한글: 가상 소멸자
        virtual ~AsyncIOProvider() = default;

        // ==================== Initialization ====================

        // 영문: Initialize the async I/O provider
        // 한글: 비동기 I/O 공급자 초기화
        // 
        // Parameters:
        //   maxConnections: Maximum number of concurrent connections
        //   maxCompletionQueueSize: Size of completion queue
        // 
        // Returns: true if successful, false otherwise
        virtual bool Initialize(
            uint32_t maxConnections,
            uint32_t maxCompletionQueueSize = 256
        ) = 0;

        // 영문: Shutdown the async I/O provider
        // 한글: 비동기 I/O 공급자 종료
        virtual void Shutdown() = 0;

        // ==================== Socket Registration ====================

        // 영문: Register a socket with the async I/O provider
        // 한글: 소켓을 비동기 I/O 공급자에 등록
        //
        // Parameters:
        //   socket: Native socket descriptor
        //   context: User-defined context (returned in completion)
        //
        // Returns: Handle for this registration (for later operations)
        virtual bool RegisterSocket(
            SOCKET socket,
            void* context = nullptr
        ) = 0;

        // 영문: Unregister a socket from the async I/O provider
        // 한글: 소켓을 비동기 I/O 공급자에서 등록 해제
        virtual bool UnregisterSocket(SOCKET socket) = 0;

        // ==================== Async Operations ====================

        // 영문: Post an async send operation
        // 한글: 비동기 송신 작업 게시
        //
        // Parameters:
        //   socket: Target socket
        //   buffer: Data to send
        //   length: Number of bytes to send
        //   callback: Completion callback
        //
        // Returns: Operation ID (for tracking/cancellation)
        virtual uint32_t SendAsync(
            SOCKET socket,
            const void* buffer,
            uint32_t length,
            CompletionCallback callback = nullptr
        ) = 0;

        // 영문: Post an async receive operation
        // 한글: 비동기 수신 작업 게시
        virtual uint32_t RecvAsync(
            SOCKET socket,
            void* buffer,
            uint32_t maxLength,
            CompletionCallback callback = nullptr
        ) = 0;

        // 영문: Post an async accept operation (server-side)
        // 한글: 비동기 수락 작업 게시 (서버측)
        virtual uint32_t AcceptAsync(
            SOCKET listeningSocket,
            SOCKET* acceptSocket,
            void* addressBuffer,
            uint32_t addressBufferSize,
            CompletionCallback callback = nullptr
        ) = 0;

        // 영문: Post an async connect operation (client-side)
        // 한글: 비동기 연결 작업 게시 (클라이언트측)
        virtual uint32_t ConnectAsync(
            SOCKET socket,
            const struct sockaddr* remoteAddress,
            int addressLength,
            CompletionCallback callback = nullptr
        ) = 0;

        // ==================== Batch Operations ====================

        // 영문: Post multiple async send operations as a batch
        // 한글: 여러 비동기 송신 작업을 배치로 게시
        //
        // Parameters:
        //   operations: Array of send operations
        //   count: Number of operations in array
        //
        // Returns: Number of operations successfully posted
        virtual uint32_t SendBatchAsync(
            const struct SendOperation* operations,
            uint32_t count
        ) = 0;

        // 영문: Post multiple async receive operations as a batch
        // 한글: 여러 비동기 수신 작업을 배치로 게시
        virtual uint32_t RecvBatchAsync(
            const struct RecvOperation* operations,
            uint32_t count
        ) = 0;

        // ==================== Completion Processing ====================

        // 영문: Process pending completions (blocking, with timeout)
        // 한글: 대기 중인 완료 작업 처리 (블로킹, 타임아웃 포함)
        //
        // Parameters:
        //   timeoutMs: Maximum time to wait (0 = non-blocking, -1 = infinite)
        //   maxResults: Maximum number of results to return
        //   results: [OUT] Array of completion results
        //
        // Returns:
        //   > 0: Number of completions returned (successful operations completed)
        //   = 0: Timeout occurred (no completions, no error)
        //   < 0: Error occurred (error code in CompletionResult, see ErrorCode enum)
        virtual int32_t ProcessCompletions(
            int32_t timeoutMs,
            uint32_t maxResults,
            CompletionResult* results
        ) = 0;

        // ==================== Configuration ====================

        // 영문: Get the name of the current backend implementation
        // 한글: 현재 백엔드 구현의 이름 조회
        virtual const char* GetBackendName() const = 0;

        // 영문: Get backend-specific capabilities
        // 한글: 백엔드별 기능 조회
        virtual bool SupportsFeature(const char* featureName) const = 0;

        // 영문: Set backend-specific option
        // 한글: 백엔드별 옵션 설정
        virtual bool SetOption(
            const char* optionName,
            const void* value,
            uint32_t valueSize
        ) = 0;

        // ==================== Diagnostics ====================

        // 영문: Get current statistics (connections, pending ops, etc.)
        // 한글: 현재 통계 조회 (연결, 대기 작업 등)
        virtual void GetStatistics(struct ProviderStatistics& outStats) const = 0;
    };
}
```

### 2. Operation Structures

```cpp
// 영문: Send operation descriptor for batch operations
// 한글: 배치 작업용 송신 작업 설명자
struct SendOperation
{
    SOCKET mSocket;
    const void* mBuffer;
    uint32_t mLength;
    void* mUserContext;
    CompletionCallback mCallback;
};

// 영문: Receive operation descriptor for batch operations
// 한글: 배치 작업용 수신 작업 설명자
struct RecvOperation
{
    SOCKET mSocket;
    void* mBuffer;
    uint32_t mMaxLength;
    void* mUserContext;
    CompletionCallback mCallback;
};

// 영문: Provider-level statistics
// 한글: 공급자 수준의 통계
struct ProviderStatistics
{
    // 영문: Total number of active connections
    // 한글: 활성 연결의 총 개수
    uint32_t mActiveConnections;

    // 영문: Total pending operations
    // 한글: 대기 중인 작업의 총 개수
    uint32_t mPendingOperations;

    // 영문: Total completed operations since startup
    // 한글: 시작 후 완료된 작업의 총 개수
    uint64_t mCompletedOperations;

    // 영문: Total failed operations
    // 한글: 실패한 작업의 총 개수
    uint64_t mFailedOperations;

    // 영문: Average latency in milliseconds
    // 한글: 평균 레이턴시 (밀리초)
    float mAverageLatencyMs;

    // 영문: Backend-specific data (interpretation depends on backend)
    // 한글: 백엔드별 데이터 (백엔드에 따라 해석)
    void* mBackendSpecificData;
};
```

---

## Platform-Specific Implementations

### Windows: IocpAsyncIOProvider

```cpp
// 영문: IOCP-based implementation for Windows
// 한글: Windows용 IOCP 기반 구현

class IocpAsyncIOProvider : public AsyncIOProvider
{
    // Features:
    // - IOCP Completion Port
    // - GetQueuedCompletionStatus for batch processing
    // - Compatible with existing RAON code
    // - Performance: Good (baseline)
};
```

### Windows: RIOAsyncIOProvider

```cpp
// 영문: Registered I/O (RIO) implementation for Windows 8+
// 한글: Windows 8+ 용 등록 I/O (RIO) 구현

class RIOAsyncIOProvider : public AsyncIOProvider
{
    // Features:
    // - Request Queue (SQ) + Completion Queue (CQ)
    // - RIOSend/RIOReceive with pre-registered buffers
    // - User-managed buffer registration
    // - Performance: 3x faster than IOCP (1.2M → 3.6M ops/sec)
    // Requirement: Windows 8+ (Winsock 2 RIO extension)
};
```

### Linux: IOUringAsyncIOProvider

```cpp
// 영문: io_uring implementation for Linux 5.1+
// 한글: Linux 5.1+ 용 io_uring 구현

class IOUringAsyncIOProvider : public AsyncIOProvider
{
    // Features:
    // - Submission Queue (SQ) + Completion Queue (CQ)
    // - Ring-buffer based (zero-copy semantics)
    // - Support for fixed file sets
    // - Performance: 4x faster than epoll (850 → 120 μsec latency)
    // Requirement: Linux 5.1+ with io_uring support
};
```

### Linux: EpollAsyncIOProvider

```cpp
// 영문: epoll implementation for Linux (fallback)
// 한글: Linux용 epoll 구현 (대체)

class EpollAsyncIOProvider : public AsyncIOProvider
{
    // Features:
    // - Level-triggered or edge-triggered
    // - epoll_wait for batch processing
    // - Compatible with older Linux distributions
    // - Performance: Baseline for Linux
    // Requirement: Any modern Linux (2.6+)
};
```

---

## Usage Patterns

### Pattern 1: Basic Initialization

```cpp
// 영문: Initialize the async I/O provider with platform detection
// 한글: 플랫폼 감지와 함께 비동기 I/O 공급자 초기화

#include <Network/AsyncIOProvider.h>

std::unique_ptr<Network::AsyncIOProvider> provider;

// 영문: Attempt to use the best available backend
// 한글: 사용 가능한 최적의 백엔드 시도
if constexpr (PLATFORM_WINDOWS)
{
    // 영문: Try RIO first (Windows 8+), then IOCP
    // 한글: 먼저 RIO 시도 (Windows 8+), 다음 IOCP
    provider = std::make_unique<Network::RIOAsyncIOProvider>();
    if (!provider->Initialize(1000, 256))
    {
        provider = std::make_unique<Network::IocpAsyncIOProvider>();
        provider->Initialize(1000, 256);
    }
}
else if constexpr (PLATFORM_LINUX)
{
    // 영문: Try io_uring first (Linux 5.1+), then epoll
    // 한글: 먼저 io_uring 시도 (Linux 5.1+), 다음 epoll
    provider = std::make_unique<Network::IOUringAsyncIOProvider>();
    if (!provider->Initialize(1000, 256))
    {
        provider = std::make_unique<Network::EpollAsyncIOProvider>();
        provider->Initialize(1000, 256);
    }
}

// Log which backend was selected
std::cout << "Using backend: " << provider->GetBackendName() << std::endl;
```

### Pattern 2: Simple Send/Recv

```cpp
// 영문: Simple async send with completion callback
// 한글: 완료 콜백이 있는 간단한 비동기 송신

const char* data = "Hello, Server!";
uint32_t length = std::strlen(data);

provider->SendAsync(
    clientSocket,
    data,
    length,
    [](const Network::CompletionResult& result, void* context)
    {
        if (result.mStatus == Network::CompletionResult::Status::Success)
        {
            std::cout << "Sent " << result.mBytesTransferred << " bytes\n";
        }
        else
        {
            std::cerr << "Send failed: " << result.mErrorCode << "\n";
        }
    }
);
```

### Pattern 3: Event Loop with Polling

```cpp
// 영문: Main event loop processing completions
// 한글: 완료 작업을 처리하는 메인 이벤트 루프

std::array<Network::CompletionResult, 64> completions;

while (isRunning)
{
    // 영문: Process completions with 100ms timeout
    // 한글: 100ms 타임아웃으로 완료 작업 처리
    uint32_t numCompletions = provider->ProcessCompletions(
        100,  // timeout in ms
        completions.size(),
        completions.data()
    );

    for (uint32_t i = 0; i < numCompletions; ++i)
    {
        const auto& result = completions[i];

        // 영문: Handle completion based on operation type
        // 한글: 작업 타입에 따라 완료 처리
        HandleCompletion(result);
    }
}
```

---

## Error Handling

### Platform-Independent Error Codes

```cpp
enum class ErrorCode
{
    // 영문: Connection refused (platform-agnostic)
    // 한글: 연결 거부 (플랫폼 비종속적)
    ConnectionRefused = 1001,

    // 영문: Connection reset by peer
    // 한글: 피어가 연결 재설정
    ConnectionReset = 1002,

    // 영문: Connection timeout
    // 한글: 연결 타임아웃
    ConnectionTimeout = 1003,

    // 영문: Buffer too small for operation
    // 한글: 작업용 버퍼가 너무 작음
    BufferTooSmall = 2001,

    // 영문: Socket not registered with provider
    // 한글: 소켓이 공급자에 등록되지 않음
    SocketNotRegistered = 3001,
};
```

### ProcessCompletions() Return Value Semantics

The `ProcessCompletions()` method uses the return value to communicate operation status:

```cpp
// 영문: ProcessCompletions return value meanings
// 한글: ProcessCompletions 반환값 의미
int32_t result = provider->ProcessCompletions(100, 64, completions);

if (result > 0)
{
    // 영문: Success - 'result' completions available in completions[] array
    // 한글: 성공 - completions[] 배열에 'result'개 완료 작업 사용 가능
    uint32_t numCompletions = static_cast<uint32_t>(result);
    for (uint32_t i = 0; i < numCompletions; ++i)
    {
        ProcessCompletion(completions[i]);
    }
}
else if (result == 0)
{
    // 영문: Timeout - no completions available within timeout period
    // 한글: 타임아웃 - 타임아웃 기간 내에 완료 없음
    std::cout << "No completions available\n";
}
else  // result < 0
{
    // 영문: Error - system error occurred in ProcessCompletions itself
    // 한글: 에러 - ProcessCompletions 자체에서 시스템 에러 발생
    // Note: completions[0] contains error details
    ErrorCode error = static_cast<ErrorCode>(completions[0].mErrorCode);
    std::cerr << "ProcessCompletions failed: " << static_cast<int>(error) << "\n";
}
```

### Platform Error Code Mapping

Different platforms use different error code conventions. AsyncIOProvider maps them to platform-independent ErrorCode enum:

#### Windows (IOCP / RIO)

| WSAERROR | ErrorCode Mapping | Notes |
|----------|-------------------|-------|
| WSAECONNREFUSED | ConnectionRefused (1001) | Connection actively refused by server |
| WSAECONNRESET | ConnectionReset (1002) | Connection reset by peer (RST) |
| WSAETIMEDOUT | ConnectionTimeout (1003) | Connection timeout in connect/send/recv |
| WSAENOBUFS | BufferTooSmall (2001) | Insufficient buffer space |
| WSAEINVAL | SocketNotRegistered (3001) | Socket not registered with provider |

```cpp
// 영문: Windows error conversion example (IOCP)
// 한글: Windows 에러 변환 예시 (IOCP)
DWORD dwError = WSAGetLastError();
ErrorCode errorCode;

switch (dwError)
{
    case WSAECONNREFUSED:
        errorCode = ErrorCode::ConnectionRefused;
        break;
    case WSAECONNRESET:
        errorCode = ErrorCode::ConnectionReset;
        break;
    // ... more cases
    default:
        errorCode = static_cast<ErrorCode>(dwError);  // Platform-specific
}
```

#### Linux (epoll / io_uring)

| errno | ErrorCode Mapping | Notes |
|-------|-------------------|-------|
| ECONNREFUSED | ConnectionRefused (1001) | Connection actively refused |
| ECONNRESET | ConnectionReset (1002) | Connection reset by peer |
| ETIMEDOUT | ConnectionTimeout (1003) | Operation timeout |
| ENOBUFS | BufferTooSmall (2001) | No buffer space available |
| EBADF | SocketNotRegistered (3001) | Bad file descriptor (not registered) |

```cpp
// 영문: Linux error conversion example (io_uring)
// 한글: Linux 에러 변환 예시 (io_uring)
int linuxError = -cqe->res;  // io_uring stores error as negative
ErrorCode errorCode;

switch (linuxError)
{
    case ECONNREFUSED:
        errorCode = ErrorCode::ConnectionRefused;
        break;
    case ECONNRESET:
        errorCode = ErrorCode::ConnectionReset;
        break;
    // ... more cases
    default:
        errorCode = static_cast<ErrorCode>(linuxError);  // Platform-specific
}
```

### Error Handling Patterns

#### Pattern 1: Handling Timeouts (return == 0)

```cpp
// 영문: Timeout handling pattern
// 한글: 타임아웃 처리 패턴
std::array<Network::CompletionResult, 64> completions;
uint32_t idleTimeoutMs = 5000;  // 5 second idle timeout

while (isRunning)
{
    // 영문: ProcessCompletions with 100ms timeout
    // 한글: 100ms 타임아웃으로 ProcessCompletions 호출
    int32_t result = provider->ProcessCompletions(100, completions.size(), completions.data());

    if (result > 0)
    {
        // 영문: Process completions normally
        // 한글: 완료 작업 정상 처리
        for (int32_t i = 0; i < result; ++i)
        {
            HandleCompletion(completions[i]);
        }
        idleCounter = 0;
    }
    else if (result == 0)
    {
        // 영문: Timeout - no completions available
        // 한글: 타임아웃 - 완료 작업 없음
        ++idleCounter;

        // 영문: Check for idle condition (e.g., server shutdown)
        // 한글: 유휴 상태 확인 (예: 서버 종료)
        if (idleCounter * 100 > idleTimeoutMs)
        {
            std::cout << "Idle timeout reached, checking server state\n";
            idleCounter = 0;
        }
    }
    else
    {
        // 영문: Error in ProcessCompletions
        // 한글: ProcessCompletions에서 에러 발생
        std::cerr << "ProcessCompletions error: " << result << "\n";
        break;
    }
}
```

#### Pattern 2: Handling Errors (return < 0)

```cpp
// 영문: Error handling pattern with recovery
// 한글: 복구를 포함한 에러 처리 패턴
std::array<Network::CompletionResult, 64> completions;

int32_t result = provider->ProcessCompletions(1000, completions.size(), completions.data());

if (result < 0)
{
    // 영문: System error - details in completions[0]
    // 한글: 시스템 에러 - completions[0]에 상세 정보
    const auto& errorResult = completions[0];
    Network::ErrorCode error = static_cast<Network::ErrorCode>(errorResult.mErrorCode);

    std::cerr << "ProcessCompletions failed with error: " << static_cast<int>(error) << "\n";

    // 영문: Handle specific error codes
    // 한글: 특정 에러 코드 처리
    switch (error)
    {
        case Network::ErrorCode::BufferTooSmall:
        {
            // 영문: Increase completion queue size and retry
            // 한글: 완료 큐 크기 증가 및 재시도
            std::cout << "Increasing completion queue size\n";
            // Recreate provider with larger queue
            break;
        }

        case Network::ErrorCode::SocketNotRegistered:
        {
            // 영文: Re-register all sockets
            // 한글: 모든 소켓 재등록
            std::cout << "Re-registering sockets\n";
            ReregisterAllSockets();
            break;
        }

        default:
        {
            // 영문: Unrecoverable error - shutdown
            // 한글: 복구 불가능한 에러 - 종료
            std::cerr << "Unrecoverable error, shutting down\n";
            isRunning = false;
            break;
        }
    }
}
```

#### Pattern 3: Mixed Success with Individual Errors

```cpp
// 영文: Handling mixed results (some completions, some with status=Error)
// 한글: 혼합 결과 처리 (일부 완료, 일부 상태=에러)
int32_t result = provider->ProcessCompletions(100, completions.size(), completions.data());

if (result > 0)
{
    // 영문: Process all completions (mix of Success and Error)
    // 한글: 모든 완료 작업 처리 (성공과 에러 혼합)
    uint32_t numCompletions = static_cast<uint32_t>(result);

    for (uint32_t i = 0; i < numCompletions; ++i)
    {
        const auto& completion = completions[i];

        // 영문: Check status of individual completion
        // 한글: 각 완료의 상태 확인
        switch (completion.mStatus)
        {
            case Network::CompletionResult::Status::Success:
            {
                // 영문: Operation succeeded
                // 한글: 작업 성공
                std::cout << "Operation " << completion.mOperationId 
                          << " completed: " << completion.mBytesTransferred 
                          << " bytes\n";
                break;
            }

            case Network::CompletionResult::Status::Error:
            {
                // 영文: Operation failed - handle error per operation
                // 한글: 작업 실패 - 작업별로 에러 처리
                Network::ErrorCode error = static_cast<Network::ErrorCode>(completion.mErrorCode);
                std::cerr << "Operation " << completion.mOperationId 
                          << " failed with error: " << static_cast<int>(error) << "\n";

                // 영문: Decide whether to retry, skip, or fail
                // 한글: 재시도, 건너뛰기, 실패 중 결정
                HandleOperationError(completion);
                break;
            }

            case Network::CompletionResult::Status::Timeout:
            {
                // 영문: Operation timed out
                // 한글: 작업 타임아웃
                std::cout << "Operation " << completion.mOperationId << " timed out\n";
                HandleTimeout(completion);
                break;
            }

            case Network::CompletionResult::Status::Cancelled:
            {
                // 영문: Operation was cancelled
                // 한글: 작업이 취소됨
                std::cout << "Operation " << completion.mOperationId << " cancelled\n";
                break;
            }
        }
    }
}
```

---

## Advanced Features

### Feature 1: Buffer Registration (Pre-allocation)

```cpp
// 영문: Register buffers for zero-copy operations (RIO, io_uring)
// 한글: 제로카피 작업을 위한 버퍼 등록 (RIO, io_uring)

// Feature available on: RIO, io_uring
if (provider->SupportsFeature("BufferRegistration"))
{
    // 영문: Pre-allocate and register buffers
    // 한글: 버퍼 사전 할당 및 등록
    std::vector<char> buffer(65536);
    provider->SetOption("RegisterBuffer", buffer.data(), buffer.size());
}
```

### Feature 2: Fixed File Sets (io_uring)

```cpp
// 영문: Register a set of file descriptors for fixed operations
// 한글: 고정 작업을 위한 파일 설명자 세트 등록

if (provider->SupportsFeature("FixedFileSet"))
{
    std::vector<int> fileDescriptors = { /* ... */ };
    provider->SetOption("RegisterFileSet", fileDescriptors.data(), 
                       fileDescriptors.size() * sizeof(int));
}
```

### Feature 3: Memory Safety & Error Recovery

#### Scenario 1: CompletionResult Array Too Small

**Problem**: If `maxResults` parameter is too small, pending completions may be dropped.

**Solution**:
```cpp
// 영문: Safe completion processing with overflow check
// 한글: 오버플로우 검사를 포함한 안전한 완료 처리

static const uint32_t COMPLETION_BATCH_SIZE = 256;  // Safe batch size
std::array<Network::CompletionResult, COMPLETION_BATCH_SIZE> completions;

int32_t result = provider->ProcessCompletions(100, completions.size(), completions.data());

// 영文: Always check for overflow
// 한글: 항상 오버플로우 확인
if (result == -static_cast<int32_t>(completions.size()))
{
    // 영文: Queue overflow - too many completions, need larger array
    // 한글: 큐 오버플로우 - 완료 작업이 너무 많음, 더 큰 배열 필요
    std::cerr << "Completion queue overflow, increasing buffer size\n";
    COMPLETION_BATCH_SIZE *= 2;  // Double the size
}
```

#### Scenario 2: Callback Must Not Throw

**Problem**: Exception in callback can break completion processing chain.

**Solution**:
```cpp
// 영문: Safe callback pattern with exception handling
// 한글: 예외 처리를 포함한 안전한 콜백 패턴

auto safeCallback = [this](const Network::CompletionResult& result, void* context) noexcept
{
    // 영문: Mark callback as noexcept - exception safety guarantee
    // 한글: 콜백을 noexcept로 표시 - 예외 안전성 보장
    try
    {
        HandleCompletionUnsafe(result, context);
    }
    catch (const std::exception& e)
    {
        // 영文: Log error but don't throw - provider continues
        // 한글: 에러 로깅 하되 예외 발생 안함 - 공급자 계속 실행
        std::cerr << "Callback exception: " << e.what() << "\n";
    }
    catch (...)
    {
        // 영文: Catch-all for unexpected exceptions
        // 한글: 예상치 못한 예외 포괄 처리
        std::cerr << "Unknown callback exception\n";
    }
};

provider->SendAsync(socket, data, length, safeCallback);
```

#### Scenario 3: User Context Validity

**Problem**: User context pointer may be invalid when callback fires (e.g., object deleted).

**Solution (using RAII wrapper)**:
```cpp
// 영문: RAII wrapper for safe user context management
// 한글: 안전한 사용자 컨텍스트 관리를 위한 RAII 래퍼

class SafeContextWrapper
{
public:
    // 영문: Store context with reference count
    // 한글: 참조 카운트를 포함한 컨텍스트 저장
    SafeContextWrapper(void* context) 
        : mContext(context), mRefCount(1)
    {
    }

    // 영문: Increment reference count when used
    // 한글: 사용 시 참조 카운트 증가
    void AddRef()
    {
        ++mRefCount;
    }

    // 영文: Decrement and check validity
    // 한글: 감소 및 유효성 확인
    bool Release()
    {
        --mRefCount;
        return mRefCount > 0;
    }

    void* GetContext() const
    {
        return mContext;
    }

private:
    void* mContext;
    std::atomic<int> mRefCount;
};

// 영文: Usage in callback
// 한글: 콜백에서의 사용
std::shared_ptr<SafeContextWrapper> contextWrapper = std::make_shared<SafeContextWrapper>(userContext);

auto safeCallback = [contextWrapper](const Network::CompletionResult& result, void* context) noexcept
{
    // 영文: Context is guaranteed valid while shared_ptr alive
    // 한글: shared_ptr이 유효한 동안 컨텍스트 보장
    if (contextWrapper)
    {
        void* userCtx = contextWrapper->GetContext();
        if (userCtx != nullptr)
        {
            // 영文: Safe to use context
            // 한글: 컨텍스트 안전하게 사용
        }
    }
};

provider->SendAsync(socket, data, length, safeCallback);
```

#### Scenario 4: Provider Shutdown with Pending Operations

**Problem**: Shutting down provider while operations are pending.

**Solution**:
```cpp
// 영문: Safe shutdown pattern
// 한글: 안전한 종료 패턴

class AsyncIOManager
{
private:
    std::unique_ptr<Network::AsyncIOProvider> mProvider;
    std::atomic<uint32_t> mPendingOperations{0};

public:
    void SendAsyncSafe(SOCKET socket, const void* data, uint32_t length)
    {
        ++mPendingOperations;

        auto callback = [this](const Network::CompletionResult& result, void* context) noexcept
        {
            // 영文: Always decrement pending count
            // 한글: 항상 대기 중인 작업 수 감소
            --mPendingOperations;
        };

        mProvider->SendAsync(socket, data, length, callback);
    }

    void Shutdown()
    {
        // 영文: Wait for all pending operations to complete
        // 한글: 모든 대기 중인 작업이 완료될 때까지 대기
        std::array<Network::CompletionResult, 64> completions;
        
        while (mPendingOperations > 0)
        {
            int32_t result = mProvider->ProcessCompletions(100, completions.size(), completions.data());
            
            if (result < 0)
            {
                // 영문: Error during shutdown - force close
                // 한글: 종료 중 에러 - 강제 종료
                std::cerr << "Error during shutdown\n";
                break;
            }
        }

        // 영文: Now safe to shut down
        // 한글: 이제 안전하게 종료 가능
        mProvider->Shutdown();
    }
};
```

### Feature 4: Buffer Registration for RIO & io_uring

Zero-copy performance optimization requires pre-registered buffers for RIO (Windows) and io_uring (Linux).

#### API Design

```cpp
// 영문: Add to AsyncIOProvider interface
// 한글: AsyncIOProvider 인터페이스에 추가

class AsyncIOProvider
{
public:
    // ==================== Buffer Registration ====================

    // 영문: Register a buffer for zero-copy operations
    // 한글: 제로카피 작업용 버퍼 등록
    //
    // Parameters:
    //   buffer: Pointer to buffer memory (must remain valid until unregistered)
    //   size: Buffer size in bytes
    //
    // Returns:
    //   >= 0: Buffer ID (for use in SendAsyncRegistered/RecvAsyncRegistered)
    //   < 0: Error code
    virtual int32_t RegisterBuffer(const void* buffer, uint32_t size) = 0;

    // 영문: Unregister a previously registered buffer
    // 한글: 이전에 등록된 버퍼 등록 해제
    //
    // Parameters:
    //   bufferId: Buffer ID returned by RegisterBuffer
    //
    // Returns:
    //   true if unregistered successfully, false otherwise
    virtual bool UnregisterBuffer(int32_t bufferId) = 0;

    // 영문: Send using a pre-registered buffer
    // 한글: 사전 등록된 버퍼를 사용하여 송신
    //
    // Parameters:
    //   socket: Target socket
    //   registeredBufferId: Buffer ID from RegisterBuffer
    //   offset: Offset into registered buffer
    //   length: Number of bytes to send
    //   callback: Completion callback
    //
    // Returns:
    //   Operation ID for tracking
    virtual uint32_t SendAsyncRegistered(
        SOCKET socket,
        int32_t registeredBufferId,
        uint32_t offset,
        uint32_t length,
        CompletionCallback callback = nullptr
    ) = 0;

    // 영文: Receive into a pre-registered buffer
    // 한글: 사전 등록된 버퍼로 수신
    virtual uint32_t RecvAsyncRegistered(
        SOCKET socket,
        int32_t registeredBufferId,
        uint32_t offset,
        uint32_t maxLength,
        CompletionCallback callback = nullptr
    ) = 0;
};
```

#### RIO Implementation

```cpp
// 영文: Windows RIO buffer registration
// 한글: Windows RIO 버퍼 등록

class RIOAsyncIOProvider : public AsyncIOProvider
{
private:
    // 영文: Map of registered buffers
    // 한글: 등록된 버퍼의 맵
    struct RegisteredBufferInfo
    {
        RIO_BUFFERID mRioBufferId;
        const void* mPtr;
        uint32_t mSize;
        uint32_t mRefCount;  // For reference counting
    };
    std::map<int32_t, RegisteredBufferInfo> mRegisteredBuffers;
    int32_t mNextBufferId = 1;
    std::mutex mBufferMutex;

public:
    int32_t RegisterBuffer(const void* buffer, uint32_t size) override
    {
        if (!buffer || size == 0)
            return -1;  // Invalid parameter

        std::lock_guard<std::mutex> lock(mBufferMutex);

        // 영文: Call RIO buffer registration
        // 한글: RIO 버퍼 등록 호출
        RIO_BUFFERID rioBufferId = RIORegisterBuffer(
            const_cast<void*>(buffer),
            size
        );

        if (rioBufferId == RIO_INVALID_BUFFERID)
            return -1;  // Registration failed

        // 영문: Store mapping
        // 한글: 매핑 저장
        int32_t bufferId = mNextBufferId++;
        mRegisteredBuffers[bufferId] = {
            rioBufferId,
            buffer,
            size,
            1  // Reference count
        };

        return bufferId;
    }

    bool UnregisterBuffer(int32_t bufferId) override
    {
        std::lock_guard<std::mutex> lock(mBufferMutex);

        auto it = mRegisteredBuffers.find(bufferId);
        if (it == mRegisteredBuffers.end())
            return false;  // Buffer not found

        // 영문: Decrement reference count
        // 한글: 참조 카운트 감소
        --it->second.mRefCount;

        if (it->second.mRefCount == 0)
        {
            // 영문: Deregister from RIO when refcount reaches zero
            // 한글: 참조 카운트가 0이 되면 RIO에서 등록 해제
            RIODeregisterBuffer(it->second.mRioBufferId);
            mRegisteredBuffers.erase(it);
        }

        return true;
    }

    uint32_t SendAsyncRegistered(
        SOCKET socket,
        int32_t registeredBufferId,
        uint32_t offset,
        uint32_t length,
        CompletionCallback callback = nullptr
    ) override
    {
        std::lock_guard<std::mutex> lock(mBufferMutex);

        auto it = mRegisteredBuffers.find(registeredBufferId);
        if (it == mRegisteredBuffers.end())
            return 0;  // Invalid buffer ID

        const auto& bufInfo = it->second;

        // 영文: Validate offset and length
        // 한글: 오프셋과 길이 유효성 검사
        if (offset + length > bufInfo.mSize)
            return 0;  // Out of bounds

        // 영문: Create RIO_BUF structure
        // 한글: RIO_BUF 구조체 생성
        RIO_BUF rioBuf;
        rioBuf.BufferId = bufInfo.mRioBufferId;
        rioBuf.Offset = offset;
        rioBuf.Length = length;

        // 영文: Store request for tracking
        // 한글: 요청 추적을 위해 저장
        uint32_t operationId = ++mNextOperationId;

        // 영文: Post send to RIO
        // 한글: RIO에 송신 게시
        RIOSend(mRQ, &rioBuf, 1, 0, operationId);

        // 영文: Store callback
        // 한글: 콜백 저장
        if (callback)
        {
            mCallbacks[operationId] = callback;
        }

        return operationId;
    }

    uint32_t RecvAsyncRegistered(
        SOCKET socket,
        int32_t registeredBufferId,
        uint32_t offset,
        uint32_t maxLength,
        CompletionCallback callback = nullptr
    ) override
    {
        // 영文: Similar to SendAsyncRegistered
        // 한글: SendAsyncRegistered와 유사
        std::lock_guard<std::mutex> lock(mBufferMutex);

        auto it = mRegisteredBuffers.find(registeredBufferId);
        if (it == mRegisteredBuffers.end())
            return 0;

        const auto& bufInfo = it->second;

        if (offset + maxLength > bufInfo.mSize)
            return 0;

        RIO_BUF rioBuf;
        rioBuf.BufferId = bufInfo.mRioBufferId;
        rioBuf.Offset = offset;
        rioBuf.Length = maxLength;

        uint32_t operationId = ++mNextOperationId;

        // 영文: Post receive to RIO
        // 한글: RIO에 수신 게시
        RIORecv(mRQ, &rioBuf, 1, 0, operationId);

        if (callback)
        {
            mCallbacks[operationId] = callback;
        }

        return operationId;
    }
};
```

#### Usage Pattern: Buffer Pool

```cpp
// 영문: Buffer pool pattern for efficient buffer reuse
// 한글: 효율적인 버퍼 재사용을 위한 버퍼 풀 패턴

class BufferPool
{
private:
    static const uint32_t BUFFER_SIZE = 65536;
    static const uint32_t POOL_SIZE = 16;

    struct PooledBuffer
    {
        std::array<char, BUFFER_SIZE> data;
        int32_t registeredId = -1;
        bool inUse = false;
    };

    std::vector<PooledBuffer> mBuffers;
    std::queue<size_t> mFreeBuffers;
    Network::AsyncIOProvider* mProvider;

public:
    BufferPool(Network::AsyncIOProvider* provider)
        : mProvider(provider)
    {
        mBuffers.resize(POOL_SIZE);

        // 영문: Register all buffers upfront
        // 한글: 모든 버퍼를 미리 등록
        for (size_t i = 0; i < mBuffers.size(); ++i)
        {
            int32_t bufferId = mProvider->RegisterBuffer(
                mBuffers[i].data.data(),
                BUFFER_SIZE
            );

            if (bufferId >= 0)
            {
                mBuffers[i].registeredId = bufferId;
                mFreeBuffers.push(i);
            }
        }
    }

    ~BufferPool()
    {
        // 영문: Unregister all buffers
        // 한글: 모든 버퍼 등록 해제
        for (auto& buf : mBuffers)
        {
            if (buf.registeredId >= 0)
            {
                mProvider->UnregisterBuffer(buf.registeredId);
            }
        }
    }

    // 영文: Acquire a buffer from pool
    // 한글: 풀에서 버퍼 획득
    int32_t AcquireBuffer()
    {
        if (mFreeBuffers.empty())
            return -1;  // Pool exhausted

        size_t idx = mFreeBuffers.front();
        mFreeBuffers.pop();

        mBuffers[idx].inUse = true;
        return mBuffers[idx].registeredId;
    }

    // 영문: Release buffer back to pool
    // 한글: 버퍼를 풀로 반환
    void ReleaseBuffer(int32_t bufferId)
    {
        for (size_t i = 0; i < mBuffers.size(); ++i)
        {
            if (mBuffers[i].registeredId == bufferId)
            {
                mBuffers[i].inUse = false;
                mFreeBuffers.push(i);
                return;
            }
        }
    }

    // 영文: Send using pool buffer
    // 한글: 풀 버퍼를 사용하여 송신
    uint32_t SendRegistered(
        SOCKET socket,
        const void* data,
        uint32_t length,
        Network::AsyncIOProvider::CompletionCallback callback = nullptr
    )
    {
        int32_t bufferId = AcquireBuffer();
        if (bufferId < 0)
            return 0;  // No buffers available

        // 영문: Copy data into pooled buffer
        // 한글: 풀 버퍼로 데이터 복사
        std::memcpy(mBuffers[FindBufferIndex(bufferId)].data.data(), data, length);

        // 영文: Send with auto-release callback
        // 한글: 자동 해제 콜백과 함께 송신
        auto wrappedCallback = [this, bufferId, callback](
            const Network::CompletionResult& result,
            void* context
        ) noexcept
        {
            // 영文: Release buffer when complete
            // 한글: 완료 시 버퍼 해제
            ReleaseBuffer(bufferId);

            // 영文: Call user callback
            // 한글: 사용자 콜백 호출
            if (callback)
            {
                callback(result, context);
            }
        };

        return mProvider->SendAsyncRegistered(
            socket,
            bufferId,
            0,  // Offset
            length,
            wrappedCallback
        );
    }

private:
    size_t FindBufferIndex(int32_t registeredId)
    {
        for (size_t i = 0; i < mBuffers.size(); ++i)
        {
            if (mBuffers[i].registeredId == registeredId)
                return i;
        }
        return 0;  // Should not reach here
    }
};
```

---

### RAON Legacy IOCP Code

```cpp
// 영문: Existing RAON IocpCore can be wrapped with AsyncIOProvider
// 한글: 기존 RAON IocpCore는 AsyncIOProvider로 래핑 가능

class IocpLegacyProvider : public AsyncIOProvider
{
    // 영문: Wrapper around existing IocpCore
    // 한글: 기존 IocpCore 주위의 래퍼
    std::unique_ptr<IocpCore> mLegacyCore;
    
    // 영문: Adapts IocpCore::HandleIocp to AsyncIOProvider interface
    // 한글: IocpCore::HandleIocp를 AsyncIOProvider 인터페이스에 맞춤
};
```

---

## 메모리 안전성 패턴 (Memory Safety Patterns)

### 개요

AsyncIOProvider 사용 시 발생 가능한 메모리 안전 문제와 해결 방법을 정리합니다.

**주요 문제**:
- ❌ 콜백 중 세션 파괴 (UAF)
- ❌ 소켓 닫음 후 완료 도착 (dangling socket)
- ❌ Provider 종료 중 진행 중인 작업
- ❌ 메모리 누수 (완료 미처리)

### 패턴 1: RAII를 통한 자동 정리

```cpp
// 영문: Use RAII to ensure resources are cleaned up
// 한글: RAII를 사용하여 리소스 자동 정리

class SafeAsyncIOSession
{
private:
    AsyncIOProvider* mProvider;  // 외부에서 소유
    SocketHandle mSocket;
    std::atomic<bool> mClosing{false};
    
public:
    SafeAsyncIOSession(AsyncIOProvider* provider, SocketHandle socket)
        : mProvider(provider), mSocket(socket)
    {
    }
    
    ~SafeAsyncIOSession()
    {
        // 영문: Mark as closing to reject new requests
        // 한글: 새로운 요청 거부 표시
        mClosing = true;
        
        // 영문: Wait for pending operations
        // 한글: 진행 중인 작업 대기
        WaitForPendingOps(5000);  // 5초 제한
        
        // 영문: Close socket (prevent new completions)
        // 한글: 소켓 닫음 (새로운 완료 방지)
        if (mSocket != INVALID_SOCKET)
        {
            closesocket(mSocket);
            mSocket = INVALID_SOCKET;
        }
    }
    
    // 영문: Prevent copying (move-only semantics)
    // 한글: 복사 방지 (move-only 의미론)
    SafeAsyncIOSession(const SafeAsyncIOSession&) = delete;
    SafeAsyncIOSession& operator=(const SafeAsyncIOSession&) = delete;
    
    SafeAsyncIOSession(SafeAsyncIOSession&& other) noexcept
        : mProvider(other.mProvider), mSocket(other.mSocket)
    {
        other.mSocket = INVALID_SOCKET;
    }
};

// 사용
auto session = std::make_unique<SafeAsyncIOSession>(provider, socket);
provider->SendAsync(socket, data, size, ctx);
// session 스코프 벗어나면 자동 정리
```

### 패턴 2: 약한 참조 (Weak Reference)를 통한 UAF 방지

```cpp
// 영문: Use weak pointers to prevent UAF
// 한글: 약한 포인터로 UAF 방지

class AsyncIOSession : public std::enable_shared_from_this<AsyncIOSession>
{
private:
    AsyncIOProvider* mProvider;
    SocketHandle mSocket;
    
public:
    // 영문: Completion callback with weak reference checking
    // 한글: 약한 참조 검사가 있는 완료 콜백
    void SendDataSafe(const void* buffer, size_t size)
    {
        // 영문: Get weak reference to self
        // 한글: 자신에 대한 약한 참조 획득
        std::weak_ptr<AsyncIOSession> weakSelf = shared_from_this();
        
        // 영문: Create callback that validates session still exists
        // 한글: 세션이 여전히 존재하는지 검증하는 콜백 생성
        auto callback = [weakSelf, this](const CompletionEntry& entry)
        {
            // 영문: Try to lock weak reference
            // 한글: 약한 참조 잠금 시도
            auto session = weakSelf.lock();
            if (!session)
            {
                // 영문: Session has been destroyed - abort
                // 한글: 세션이 파괴됨 - 중단
                LOG_WARNING("Session no longer exists, ignoring completion");
                return;
            }
            
            // 영문: Safe to access session members
            // 한글: 세션 멤버에 안전하게 접근
            session->OnCompletion(entry);
        };
        
        mProvider->SendAsync(mSocket, buffer, size, callback);
    }
};

// 사용
auto session = std::make_shared<AsyncIOSession>(provider, socket);
session->SendDataSafe(data, size);
// session 파괴 → 콜백에서 약한 참조 검사 → 안전하게 무시
```

### 패턴 3: Scope-Guarded Operations

```cpp
// 영문: Scope guard to track pending operations
// 한글: 진행 중인 작업 추적 스코프 가드

class PendingOperationGuard
{
private:
    std::atomic<int>& mPendingCount;
    
public:
    PendingOperationGuard(std::atomic<int>& count)
        : mPendingCount(count)
    {
        ++mPendingCount;  // 시작 시 증가
    }
    
    ~PendingOperationGuard()
    {
        --mPendingCount;  // 종료 시 감소
    }
};

class SessionWithGuard
{
private:
    std::atomic<int> mPendingOps{0};
    std::condition_variable mAllDone;
    std::mutex mLock;
    
public:
    void SendAsync(const void* buffer, size_t size)
    {
        // 영문: Guard tracks this operation
        // 한글: 가드가 이 작업을 추적
        auto guard = std::make_unique<PendingOperationGuard>(mPendingOps);
        
        // 영문: Capture guard in callback
        // 한글: 콜백에서 가드 캡처
        auto callback = [guard = std::move(guard), this]
            (const CompletionEntry& entry)
        {
            this->OnCompletion(entry);
            // guard 범위 벗어나면 mPendingOps 감소
        };
        
        mProvider->SendAsync(mSocket, buffer, size, callback);
    }
    
    ~SessionWithGuard()
    {
        // 영문: Wait for all pending operations to complete
        // 한글: 모든 진행 중인 작업 완료 대기
        {
            std::unique_lock<std::mutex> lock(mLock);
            mAllDone.wait_for(lock, std::chrono::seconds(5),
                [this] { return mPendingOps == 0; });
        }
        
        if (mPendingOps != 0)
        {
            LOG_ERROR("Destructor: %d operations still pending",
                (int)mPendingOps);
        }
    }
};
```

### 패턴 4: Provider Shutdown Handling

```cpp
// 영문: Safe provider shutdown with operation draining
// 한글: 작업 드레이닝을 통한 안전한 Provider 종료

class ManagedAsyncIOProvider
{
private:
    std::unique_ptr<AsyncIOProvider> mProvider;
    std::atomic<bool> mShuttingDown{false};
    std::atomic<int> mPendingOps{0};
    
public:
    AsyncIOError SendAsyncGuarded(SocketHandle socket,
        const void* buffer, size_t size, RequestContext context)
    {
        // 영문: Check if shutting down
        // 한글: 종료 중인지 확인
        if (mShuttingDown)
        {
            LOG_WARNING("SendAsync during shutdown - rejected");
            return AsyncIOError::OperationFailed;
        }
        
        ++mPendingOps;
        
        // 영문: Send (exception safe)
        // 한글: 송신 (예외 안전)
        try
        {
            auto result = mProvider->SendAsync(socket, buffer, size, context);
            if (result != AsyncIOError::Success)
                --mPendingOps;
            return result;
        }
        catch (const std::exception& e)
        {
            --mPendingOps;
            LOG_ERROR("SendAsync exception: %s", e.what());
            return AsyncIOError::OperationFailed;
        }
    }
    
    int ProcessCompletionsGuarded(
        CompletionEntry* entries, size_t maxEntries, int timeoutMs)
    {
        int count = mProvider->ProcessCompletions(entries, maxEntries, timeoutMs);
        
        // 영문: Decrement for completed operations
        // 한글: 완료된 작업에 대해 감소
        mPendingOps -= count;
        
        return count;
    }
    
    void Shutdown(int drainTimeoutMs = 5000)
    {
        // 영문: Step 1: Reject new operations
        // 한글: 1단계: 새로운 작업 거부
        mShuttingDown = true;
        
        // 영문: Step 2: Drain pending operations
        // 한글: 2단계: 진행 중인 작업 드레이닝
        auto start = std::chrono::high_resolution_clock::now();
        
        while (mPendingOps > 0)
        {
            // 영문: Process remaining completions
            // 한글: 남은 완료 처리
            CompletionEntry entries[32];
            int count = mProvider->ProcessCompletions(entries, 32, 100);
            
            if (count > 0)
                mPendingOps -= count;
            
            // 영문: Check timeout
            // 한글: 타임아웃 확인
            auto elapsed = std::chrono::high_resolution_clock::now() - start;
            if (elapsed > std::chrono::milliseconds(drainTimeoutMs))
            {
                LOG_ERROR("Shutdown timeout: %d operations still pending",
                    (int)mPendingOps);
                break;
            }
        }
        
        // 영문: Step 3: Shutdown provider
        // 한글: 3단계: Provider 종료
        mProvider->Shutdown();
    }
};
```

### 패턴 5: Socket Validity Checking

```cpp
// 영문: Verify socket validity in completion handler
// 한글: 완료 핸들러에서 소켓 유효성 검사

class SocketValidator
{
private:
    // 영문: Track open sockets
    // 한글: 열린 소켓 추적
    std::unordered_set<SocketHandle> mOpenSockets;
    std::shared_mutex mLock;
    
public:
    bool IsSocketValid(SocketHandle socket)
    {
        std::shared_lock<std::shared_mutex> lock(mLock);
        return mOpenSockets.count(socket) > 0;
    }
    
    void OnSocketCreated(SocketHandle socket)
    {
        std::unique_lock<std::shared_mutex> lock(mLock);
        mOpenSockets.insert(socket);
    }
    
    void OnSocketClosed(SocketHandle socket)
    {
        std::unique_lock<std::shared_mutex> lock(mLock);
        mOpenSockets.erase(socket);
    }
    
    // 영문: Completion callback factory with validation
    // 한글: 검증이 있는 완료 콜백 팩토리
    std::function<void(const CompletionEntry&)> MakeCallback(
        SocketHandle socket,
        std::function<void(const CompletionEntry&)> handler)
    {
        return [this, socket, handler](const CompletionEntry& entry)
        {
            // 영문: Verify socket before handling
            // 한글: 처리 전에 소켓 검증
            if (!this->IsSocketValid(socket))
            {
                LOG_WARNING("Completion for closed socket %d",
                    (int)socket);
                return;
            }
            
            // 영문: Safe to handle
            // 한글: 안전하게 처리
            handler(entry);
        };
    }
};

// 사용
SocketValidator validator;

auto socket = socket();
validator.OnSocketCreated(socket);

auto callback = validator.MakeCallback(socket,
    [this](const CompletionEntry& entry)
    {
        this->OnCompletion(entry);
    });

provider->SendAsync(socket, data, size, callback);

// 나중에 소켓 닫음
closesocket(socket);
validator.OnSocketClosed(socket);
// 완료가 도착해도 안전하게 무시됨
```

### 메모리 안전성 체크리스트

- [ ] 모든 비동기 작업에 timeout 설정
- [ ] Completion callback에서 exception 처리
- [ ] RAII를 사용한 리소스 자동 정리
- [ ] 약한 참조로 UAF 방지
- [ ] Provider shutdown 시 작업 드레이닝
- [ ] 소켓 유효성 검증
- [ ] 메모리 누수 테스트 (valgrind/AddressSanitizer)
- [ ] 멀티스레드 stress 테스트
- [ ] 종료 시나리오 테스트

---

## Summary

| Feature | Windows (IOCP) | Windows (RIO) | Linux (epoll) | Linux (io_uring) | macOS (kqueue) |
|---------|---|---|---|---|---|
| **Throughput** | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ |
| **Latency** | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ |
| **Complexity** | Low | Medium | Low | High | Medium |
| **Maturity** | Stable | Stable | Stable | Growing | Stable |

---

## ProcessCompletions() Error Handling Strategy

### 개요 (Overview)

ProcessCompletions() 메서드는 비동기 I/O 완료를 처리하는 핵심 메서드입니다. 안정성 있는 에러 처리와 예외 안전성이 필수적입니다.

```
┌─────────────────────────────────┐
│ ProcessCompletions() Call       │
└────────────┬────────────────────┘
             │
     ┌───────▼────────┐
     │ 에러 분류        │
     └───┬───┬───┬────┘
         │   │   │
    ┌────▼┐ │   └────────────────────┐
    │API  │ │                        │
    │Error│ │              ┌─────────▼──────┐
    └─────┘ │              │Platform Error  │
         ┌──▼──┐           └────────────────┘
         │Buffer│
         │Error │
         └──────┘

에러 타입:
1. API 에러 (GetQueuedCompletionStatus 실패)
2. 버퍼 에러 (메모리 할당 실패)
3. 플랫폼 에러 (OS 레벨 실패)
4. 타임아웃 (타임아웃 발생)
```

### 에러 분류 및 처리

#### 1. API 에러 (OS 레벨 실패)

```cpp
// 영문: GQCS failure handling
// 한글: GQCS 실패 처리

void ProcessCompletions_WithErrorHandling(uint32_t timeoutMs)
{
    // 영문: Retrieve completions from kernel queue
    // 한글: 커널 큐에서 완료 가져오기
    
    DWORD dwNumEntries = 0;
    OVERLAPPED_ENTRY entries[MAX_COMPLETIONS];
    
    // 영문: Get completions with error handling
    // 한글: 완료 가져오기 (에러 처리 포함)
    
    BOOL success = GetQueuedCompletionStatusEx(
        mCompletionPort,
        entries,
        MAX_COMPLETIONS,
        &dwNumEntries,
        timeoutMs,
        FALSE
    );
    
    // 영문: CASE 1: GetQueuedCompletionStatusEx failed
    // 한글: 경우 1: GetQueuedCompletionStatusEx 실패
    
    if (!success)
    {
        DWORD dwError = GetLastError();
        
        // 영문: CASE 1.1: Timeout occurred (expected in most cases)
        // 한글: 경우 1.1: 타임아웃 발생 (대부분의 경우 정상)
        
        if (dwError == WAIT_TIMEOUT)
        {
            // LOG_DEBUG("ProcessCompletions: Timeout (no completions)");
            return; // 정상 - 계속 실행
        }
        
        // 영문: CASE 1.2: Completion port handle invalid
        // 한글: 경우 1.2: 완료 포트 핸들 무효
        
        if (dwError == ERROR_INVALID_HANDLE)
        {
            LOG_ERROR("ProcessCompletions: Completion port closed (IOCP shutdown?)");
            throw std::runtime_error("IOCP completion port is invalid");
        }
        
        // 영문: CASE 1.3: Invalid buffer supplied
        // 한글: 경우 1.3: 유효하지 않은 버퍼 공급됨
        
        if (dwError == ERROR_INVALID_PARAMETER)
        {
            LOG_ERROR("ProcessCompletions: Invalid parameter (buffer too small?)");
            throw std::runtime_error("Invalid parameter passed to GetQueuedCompletionStatusEx");
        }
        
        // 영문: CASE 1.4: Unexpected OS error
        // 한글: 경우 1.4: 예상치 못한 OS 에러
        
        LOG_ERROR("ProcessCompletions: OS error %lu (%s)", 
            dwError, GetErrorString(dwError).c_str());
        throw std::runtime_error(
            fmt::format("OS error in GetQueuedCompletionStatusEx: {}", dwError)
        );
    }
    
    // 영문: CASE 2: Successfully retrieved completions
    // 한글: 경우 2: 완료 성공적으로 가져옴
    
    for (DWORD i = 0; i < dwNumEntries; i++)
    {
        try
        {
            ProcessSingleCompletion(entries[i]);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("ProcessCompletions: Exception in single completion handler: %s",
                e.what());
            // 영문: Continue processing other completions
            // 한글: 다른 완료 계속 처리
        }
    }
}
```

#### 2. 세션/컨텍스트 에러

```cpp
// 영문: Session context error handling
// 한글: 세션/컨텍스트 에러 처리

void ProcessSingleCompletion(const OVERLAPPED_ENTRY& entry)
{
    // 영문: CASE 1: Null completion routine
    // 한글: 경우 1: Null 완료 루틴
    
    if (!entry.lpCompletionKey)
    {
        LOG_WARNING("ProcessCompletions: Null completion key (abnormal completion?)");
        return;
    }
    
    // 영문: CASE 2: Null OVERLAPPED structure
    // 한글: 경우 2: Null OVERLAPPED 구조체
    
    if (!entry.lpOverlapped)
    {
        LOG_ERROR("ProcessCompletions: Null OVERLAPPED structure (memory corruption?)");
        // 해당 entry를 안전하게 무시하고 계속 진행
        return;
    }
    
    // 영문: CASE 3: Extract session context
    // 한글: 경우 3: 세션 컨텍스트 추출
    
    SessionContext* ctx = static_cast<SessionContext*>(entry.lpCompletionKey);
    
    // 영문: CASE 3.1: Session was destroyed before completion arrived
    // 한글: 경우 3.1: 완료 도착 전에 세션 파괴됨
    
    if (ctx->generation != GetSessionGeneration(ctx->sessionId))
    {
        LOG_DEBUG("ProcessCompletions: Stale completion (session recreated)");
        delete ctx;
        return;
    }
    
    // 영문: CASE 3.2: Session pool lookup failed
    // 한글: 경우 3.2: 세션 풀 조회 실패
    
    auto session = mSessionPool->GetSession(ctx->sessionId);
    if (!session)
    {
        LOG_WARNING("ProcessCompletions: Session pool lookup failed (sessionId=%u)",
            ctx->sessionId);
        delete ctx;
        return;
    }
    
    // 영문: CASE 4: Operation succeeded
    // 한글: 경우 4: 작업 성공
    
    uint32_t dwBytesTransferred = entry.dwNumberOfBytesTransferred;
    
    // 영문: CASE 5: Operation failed with error code
    // 한글: 경우 5: 작업 실패 (에러 코드 포함)
    
    DWORD dwError = NO_ERROR;
    BOOL opSuccess = GetOverlappedResult(
        ctx->socket,
        entry.lpOverlapped,
        &dwBytesTransferred,
        FALSE // bWait = FALSE (이미 완료됨)
    );
    
    if (!opSuccess)
    {
        dwError = GetLastError();
        
        // 영문: Classify operation errors
        // 한글: 작업 에러 분류
        
        if (dwError == WSAECONNRESET)
        {
            LOG_WARNING("ProcessCompletions: Connection reset by peer (sessionId=%u)",
                ctx->sessionId);
            session->OnConnectionReset();
        }
        else if (dwError == WSAECONNABORTED)
        {
            LOG_WARNING("ProcessCompletions: Connection aborted (sessionId=%u)",
                ctx->sessionId);
            session->OnConnectionAborted();
        }
        else if (dwError == WSAESHUTDOWN)
        {
            LOG_DEBUG("ProcessCompletions: Socket shutdown (sessionId=%u)",
                ctx->sessionId);
            session->OnShutdown();
        }
        else
        {
            LOG_ERROR("ProcessCompletions: Operation failed with error %lu (sessionId=%u)",
                dwError, ctx->sessionId);
            session->OnOperationFailed(dwError);
        }
        
        delete ctx;
        return;
    }
    
    // 영문: CASE 6: Dispatch completion to session
    // 한글: 경우 6: 완료를 세션에 전달
    
    try
    {
        session->OnCompletion(CompletionEntry{
            .context = ctx,
            .bytesTransferred = dwBytesTransferred,
            .error = NO_ERROR
        });
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("ProcessCompletions: Exception in session handler: %s", e.what());
        // 세션 상태 비정상 - 종료 시작
        session->Close();
    }
    
    delete ctx;
}
```

#### 3. 메모리/리소스 에러

```cpp
// 영문: Memory and resource error handling
// 한글: 메모리 및 리소스 에러 처리

class CompletionErrorHandler
{
private:
    std::atomic<uint64_t> mErrorCount;
    std::atomic<uint64_t> mTimeoutCount;
    std::atomic<uint64_t> mContextErrors;
    
public:
    // 영문: Track error statistics
    // 한글: 에러 통계 추적
    
    void RecordError(ErrorType type, uint32_t errorCode)
    {
        mErrorCount++;
        
        switch (type)
        {
            case ErrorType::Timeout:
                mTimeoutCount++;
                break;
            case ErrorType::ContextError:
                mContextErrors++;
                break;
            default:
                break;
        }
        
        // 영문: Alert if error rate exceeds threshold
        // 한글: 에러율이 임계값 초과 시 경고
        
        if (mErrorCount % 1000 == 0)
        {
            uint64_t errorRate = (mErrorCount * 100) / GetTotalCompletions();
            if (errorRate > 5) // 5% 이상
            {
                LOG_WARNING("High error rate detected: %llu errors / %llu completions",
                    (unsigned long long)mErrorCount,
                    (unsigned long long)GetTotalCompletions());
            }
        }
    }
    
    // 영문: Graceful degradation
    // 한글: 우아한 성능 저하
    
    void HandleCriticalError(const std::string& reason)
    {
        LOG_CRITICAL("Critical error in ProcessCompletions: %s", reason.c_str());
        
        // 영문: 1. Stop accepting new operations
        // 한글: 1. 새로운 작업 수용 중단
        
        PauseNewOperations();
        
        // 영문: 2. Drain existing completions
        // 한글: 2. 기존 완료 처리
        
        DrainAllCompletions();
        
        // 영문: 3. Graceful shutdown
        // 한글: 3. 정상 종료
        
        InitiateShutdown();
    }
};
```

#### 4. 복구 전략 (Recovery Strategy)

```cpp
// 영문: Recovery mechanism for transient errors
// 한글: 일시적 에러에 대한 복구 메커니즘

class ResilientProcessCompletions
{
private:
    static constexpr uint32_t MAX_RETRIES = 3;
    static constexpr uint32_t RETRY_BACKOFF_MS = 10;
    
public:
    // 영문: Retry logic for transient failures
    // 한글: 일시적 실패에 대한 재시도 로직
    
    bool ProcessWithRetry(
        HANDLE completionPort,
        uint32_t timeoutMs,
        std::function<void(const OVERLAPPED_ENTRY&)> handler)
    {
        DWORD dwNumEntries = 0;
        OVERLAPPED_ENTRY entries[MAX_COMPLETIONS];
        
        for (uint32_t attempt = 0; attempt < MAX_RETRIES; attempt++)
        {
            BOOL success = GetQueuedCompletionStatusEx(
                completionPort,
                entries,
                MAX_COMPLETIONS,
                &dwNumEntries,
                timeoutMs,
                FALSE
            );
            
            if (success || GetLastError() == WAIT_TIMEOUT)
            {
                // 영문: Success or expected timeout
                // 한글: 성공 또는 예상된 타임아웃
                
                for (DWORD i = 0; i < dwNumEntries; i++)
                {
                    handler(entries[i]);
                }
                return true;
            }
            
            // 영문: Transient error - retry
            // 한글: 일시적 에러 - 재시도
            
            if (attempt < MAX_RETRIES - 1)
            {
                LOG_WARNING("ProcessCompletions: Transient error, retrying (attempt %u/%u)",
                    attempt + 1, MAX_RETRIES);
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(RETRY_BACKOFF_MS << attempt)
                );
            }
        }
        
        // 영문: All retries exhausted
        // 한글: 모든 재시도 소진
        
        DWORD dwError = GetLastError();
        LOG_ERROR("ProcessCompletions: Failed after %u retries, error: %lu",
            MAX_RETRIES, dwError);
        return false;
    }
};
```

### 구현 체크리스트

- [ ] API 에러 분류 및 처리 (CASE 1.1 - 1.4)
- [ ] 세션 컨텍스트 에러 처리 (CASE 3.1 - 3.2)
- [ ] 작업 에러 핸들링 (CASE 5)
- [ ] 메모리/리소스 추적 및 통계
- [ ] 우아한 성능 저하 메커니즘
- [ ] 복구 및 재시도 전략
- [ ] 에러 로깅 및 모니터링
- [ ] 단위 테스트 (모든 에러 경로)

---

**Next**: Implement platform-specific backends  
**Estimated Effort**: 2-3 weeks per platform

