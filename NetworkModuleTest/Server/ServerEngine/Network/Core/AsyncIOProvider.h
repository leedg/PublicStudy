#pragma once
// encoding: UTF-8

// English: Unified async I/O provider interface for all platforms
// 한글: 모든 플랫폼의 비동기 I/O를 통일하는 인터페이스

#include <cstdint>
#include <memory>
#include <vector>
#include <functional>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <mswsock.h>
    // English: Windows socket handle type
    // 한글: Windows 소켓 핸들 타입
    using SocketHandle = SOCKET;
    // English: Windows OS error type
    // 한글: Windows OS 에러 타입
    using OSError = DWORD;
#else
    #include <sys/socket.h>
    // English: POSIX socket handle type (file descriptor)
    // 한글: POSIX 소켓 핸들 타입 (파일 디스크립터)
    using SocketHandle = int;
    // English: POSIX OS error type
    // 한글: POSIX OS 에러 타입
    using OSError = int;
#endif

namespace Network {
namespace AsyncIO {
    // =============================================================================  
    // English: Type Definitions
    // 한글: 타입 정의
    // =============================================================================

    // English: User-defined context for async operations
    // 한글: 비동기 작업용 사용자 정의 컨텍스트
    using RequestContext = uint64_t;

    // English: Completion callback function type
    // 한글: 완료 콜백 함수 타입
    using CompletionCallback = std::function<void(const struct CompletionEntry&, void* userData)>;

    // =============================================================================  
    // English: Enumerations
    // 한글: 열거형
    // =============================================================================  

    // English: Async I/O operation types
    // 한글: 비동기 I/O 작업 타입
    enum class AsyncIOType : uint8_t
    {
        // English: Send operation
        // 한글: 송신 작업
        Send,

        // English: Receive operation
        // 한글: 수신 작업
        Recv,

        // English: Accept connection (listener)
        // 한글: 연결 수락 (리스너)
        Accept,

        // English: Connect request (client)
        // 한글: 연결 요청 (클라이언트)
        Connect,

        // English: Timeout (internal use)
        // 한글: 타임아웃 (내부 사용)
        Timeout,

        // English: Error (internal use)
        // 한글: 에러 (내부 사용)
        Error,
    };

    // English: Platform types (backend implementations)
    // 한글: 플랫폼 타입 (백엔드 구현)
    // Note: These represent AsyncIO BACKEND implementations, NOT OS platforms
    // 참고: OS 플랫폼이 아니라 AsyncIO 백엔드 구현을 나타냅니다
    // - Windows: Default = IOCP, High-Performance = RIO
    // - Linux: Default = epoll, High-Performance = IOUring
    // - macOS: Always = Kqueue
    enum class PlatformType : uint8_t
    {
        // English: Windows IOCP (stable, all Windows versions)
        // 한글: Windows IOCP (안정성, 모든 Windows 버전)
        IOCP,

        // English: Windows Registered I/O (high-performance, Windows 8+)
        // 한글: Windows 등록 I/O (고성능, Windows 8+)
        RIO,

        // English: Linux epoll (stable, all Linux)
        // 한글: Linux epoll (안정성, 모든 Linux)
        Epoll,

        // English: Linux io_uring (high-performance, kernel 5.1+)
        // 한글: Linux io_uring (고성능, 커널 5.1+)
        IOUring,

        // English: macOS kqueue (standard)
        // 한글: macOS kqueue (표준)
        Kqueue,
    };

    // English: Error codes for async I/O operations
    // 한글: 비동기 I/O 작업의 에러 코드
    enum class AsyncIOError : int32_t
    {
        // English: Operation completed successfully
        // 한글: 작업이 성공적으로 완료됨
        Success = 0,

        // English: Provider not initialized
        // 한글: 공급자가 초기화되지 않음
        NotInitialized = -1,

        // English: Invalid socket handle
        // 한글: 잘못된 소켓 핸들
        InvalidSocket = -2,

        // English: Operation is pending
        // 한글: 작업이 대기 중
        OperationPending = -3,

        // English: Operation failed
        // 한글: 작업이 실패함
        OperationFailed = -4,

        // English: Invalid buffer
        // 한글: 잘못된 버퍼
        InvalidBuffer = -5,

        // English: No resources available
        // 한글: 사용 가능한 리소스 없음
        NoResources = -6,

        // English: Operation timed out
        // 한글: 작업이 타임아웃됨
        Timeout = -7,

        // English: Platform not supported
        // 한글: 플랫폼이 지원되지 않음
        PlatformNotSupported = -8,

        // English: Already initialized
        // 한글: 이미 초기화됨
        AlreadyInitialized = -9,

        // English: Invalid parameter
        // 한글: 잘못된 매개변수
        InvalidParameter = -10,

        // English: Memory allocation failed
        // 한글: 메모리 할당 실패
        AllocationFailed = -11,

        // English: Resource exhausted
        // 한글: 리소스 고갈
        ResourceExhausted = -12,
    };

    // English: Buffer registration policy
    // 한글: 버퍼 등록 정책
    enum class BufferPolicy : uint8_t
    {
        // English: Buffer can be reused for multiple operations
        // 한글: 버퍼를 여러 작업에 재사용 가능
        Reuse,

        // English: Buffer is used once then freed
        // 한글: 버퍼를 한 번 사용 후 해제
        SingleUse,

        // English: Buffer is from a pool
        // 한글: 버퍼가 풀에서 제공됨
        Pooled,
    };

    // =============================================================================  
    // English: Structures
    // 한글: 구조체
    // =============================================================================  

    // English: Completion entry from I/O completion
    // 한글: I/O 완료 항목
    struct CompletionEntry
    {
        // English: Request context (user-defined ID)
        // 한글: 요청 컨텍스트 (사용자 정의 ID)
        RequestContext mContext;

        // English: Type of operation (Send/Recv/etc)
        // 한글: 작업 타입 (Send/Recv 등)
        AsyncIOType mType;

        // English: Bytes transferred or error code
        // 한글: 전송된 바이트 수 또는 에러 코드
        int32_t mResult;

        // English: System error code (0 = success)
        // 한글: 시스템 에러 코드 (0 = 성공)
        OSError mOsError;

        // English: Completion time in nanoseconds (optional)
        // 한글: 완료 시간 (나노초, 선택사항)
        uint64_t mCompletionTime;
    };

    // English: Send/Receive buffer structure
    // 한글: 송수신 버퍼 구조체
    struct IOBuffer
    {
        // English: Buffer pointer
        // 한글: 버퍼 포인터
        void* mData;

        // English: Buffer size
        // 한글: 버퍼 크기
        size_t mSize;

        // English: Offset (can be used instead of RIO BufferId)
        // 한글: 오프셋 (RIO BufferId 대신 사용 가능)
        size_t mOffset;
    };

    // English: Provider information structure
    // 한글: 공급자 정보 구조체
    struct ProviderInfo
    {
        // English: Platform type (backend implementation)
        // 한글: 플랫폼 타입 (백엔드 구현)
        PlatformType mPlatformType;

        // English: Human-readable name ("IOCP", "RIO", "io_uring", etc)
        // 한글: 사람이 읽을 수 있는 이름 ("IOCP", "RIO", "io_uring" 등)
        const char* mName;

        // English: Capabilities flags (supported features)
        // 한글: 기능 플래그 (지원 기능)
        uint32_t mCapabilities;

        // English: Maximum queue depth
        // 한글: 최대 큐 깊이
        size_t mMaxQueueDepth;

        // English: Maximum concurrent requests
        // 한글: 최대 동시 요청
        size_t mMaxConcurrentReq;

        // English: Buffer pre-registration support
        // 한글: 버퍼 사전 등록 지원
        bool mSupportsBufferReg;

        // English: Batch processing support
        // 한글: 배치 처리 지원
        bool mSupportsBatching;

        // English: Zero-copy support
        // 한글: Zero-copy 지원
        bool mSupportsZeroCopy;
    };

    // English: Provider statistics structure
    // 한글: 공급자 통계 구조체
    struct ProviderStats
    {
        // English: Total number of requests
        // 한글: 전체 요청 수
        uint64_t mTotalRequests;

        // English: Total number of completions
        // 한글: 전체 완료 수
        uint64_t mTotalCompletions;

        // English: Number of pending requests
        // 한글: 대기 중인 요청 수
        uint64_t mPendingRequests;

        // English: Average latency in nanoseconds
        // 한글: 평균 레이턴시 (나노초)
        uint64_t mAvgLatency;

        // English: P99 latency
        // 한글: P99 레이턴시
        double mP99Latency;

        // English: Error count
        // 한글: 에러 수
        uint64_t mErrorCount;
    };

    // English: Platform information (for detection)
    // 한글: 플랫폼 정보 (감지용)
    struct PlatformInfo
    {
        // English: Detected platform type
        // 한글: 감지된 플랫폼 타입
        PlatformType mPlatformType;

        // English: OS major version
        // 한글: OS 주 버전
        uint32_t mMajorVersion;

        // English: OS minor version
        // 한글: OS 부 버전
        uint32_t mMinorVersion;

        // English: Human-readable platform name
        // 한글: 사람이 읽을 수 있는 플랫폼 이름
        const char* mPlatformName;

        // English: Windows RIO support
        // 한글: Windows RIO 지원
        bool mSupportRIO;

        // English: Linux io_uring support
        // 한글: Linux io_uring 지원
        bool mSupportIOUring;

        // English: macOS kqueue support
        // 한글: macOS kqueue 지원
        bool mSupportKqueue;
    };

    // English: Buffer registration result
    // 한글: 버퍼 등록 결과
    struct BufferRegistration
    {
        // English: Buffer ID (for future reference)
        // 한글: 버퍼 ID (향후 참조용)
        int64_t mBufferId;

        // English: Registration successful?
        // 한글: 등록 성공 여부
        bool mSuccess;

        // English: Error code if failed
        // 한글: 실패 시 에러 코드
        int32_t mErrorCode;
    };

    // =============================================================================  
    // English: Abstract Interface: AsyncIOProvider
    // 한글: 추상 인터페이스: AsyncIOProvider
    // =============================================================================  

    class AsyncIOProvider
    {
    public:
        // English: Virtual destructor
        // 한글: 가상 소멸자
        virtual ~AsyncIOProvider() = default;

        // =====================================================================
        // English: Lifecycle Management
        // 한글: 생명주기 관리
        // =====================================================================

        /**
         * English: Initialize async I/O provider
         * 한글: 비동기 I/O 공급자 초기화
         * @param queueDepth Queue depth for requests/completions (32-4096)
         * @param maxConcurrent Maximum concurrent requests
         * @return Error code (Success if initialization succeeded)
         */
        virtual AsyncIOError Initialize(
            size_t queueDepth,
            size_t maxConcurrent
        ) = 0;

        /**
         * English: Shutdown async I/O provider
         * 한글: 비동기 I/O 공급자 종료
         */
        virtual void Shutdown() = 0;

        /**
         * English: Check if provider is initialized
         * 한글: 공급자 초기화 여부 확인
         * @return true if initialized
         */
        virtual bool IsInitialized() const = 0;

        // =====================================================================
        // English: Buffer Management
        // 한글: 버퍼 관리
        // =====================================================================

        /**
         * English: Register a buffer for optimized I/O (RIO/io_uring specific)
         * 한글: 최적화된 I/O용 버퍼 사전 등록 (RIO/io_uring 전용)
         * @param ptr Buffer pointer
         * @param size Buffer size
         * @return Buffer ID (>= 0 success, < 0 error)
         *
         * Note: Only meaningful for RIO/io_uring (IOCP returns no-op)
         * 참고: RIO/io_uring에서만 의미 있음 (IOCP는 no-op)
         */
        virtual int64_t RegisterBuffer(const void* ptr, size_t size) = 0;

        /**
         * English: Unregister a previously registered buffer
         * 한글: 이전에 등록된 버퍼 등록 해제
         * @param bufferId Buffer ID from RegisterBuffer
         * @return Error code
         */
        virtual AsyncIOError UnregisterBuffer(int64_t bufferId) = 0;

        // =====================================================================
        // English: Async I/O Requests
        // 한글: 비동기 I/O 요청
        // =====================================================================

        /**
         * English: Asynchronous send operation
         * 한글: 비동기 송신 작업
         * @param socket Socket handle
         * @param buffer Send buffer
         * @param size Send size
         * @param context Request ID (returned in completion)
         * @param flags Platform-specific flags
         * @return Error code
         *
         * Note: Behavior varies by platform
         * 참고: 플랫폼마다 동작이 다름
         * - IOCP: Immediate execution (flags ignored)
         * - RIO: With RIO_MSG_DEFER, waits for batch processing
         * - io_uring: Automatic batch processing
         */
        virtual AsyncIOError SendAsync(
            SocketHandle socket,
            const void* buffer,
            size_t size,
            RequestContext context,
            uint32_t flags = 0
        ) = 0;

        /**
         * English: Asynchronous receive operation
         * 한글: 비동기 수신 작업
         * @param socket Socket handle
         * @param buffer Receive buffer
         * @param size Buffer size
         * @param context Request ID
         * @param flags Platform-specific flags
         * @return Error code
         */
        virtual AsyncIOError RecvAsync(
            SocketHandle socket,
            void* buffer,
            size_t size,
            RequestContext context,
            uint32_t flags = 0
        ) = 0;

        /**
         * English: Flush pending requests (batch execution)
         * 한글: 대기 중인 요청 일괄 실행 (배치 처리)
         *
         * - IOCP: no-op (SendAsync executes immediately)
         * - RIO: Commits deferred sends/recvs to kernel
         * - io_uring: Submits all SQ entries to kernel
         *
         * @return Error code
         */
        virtual AsyncIOError FlushRequests() = 0;

        // =====================================================================
        // English: Completion Processing
        // 한글: 완료 처리
        // =====================================================================

        /**
         * English: Process completed operations (non-blocking or with timeout)
         * 한글: 완료된 작업 처리 (논블로킹 또는 타임아웃)
         * @param entries Output array of completion entries
         * @param maxEntries Array size
         * @param timeoutMs Timeout:
         *     - 0: Non-blocking (immediate return)
         *     - >0: Wait in milliseconds
         *     - -1: Infinite wait
         * @return Number of completions processed (negative = error)
         */
        virtual int ProcessCompletions(
            CompletionEntry* entries,
            size_t maxEntries,
            int timeoutMs = 0
        ) = 0;

        // =====================================================================
        // English: Information & Statistics
        // 한글: 정보 및 통계
        // =====================================================================

        /**
         * English: Get provider information
         * 한글: 공급자 정보 조회
         */
        virtual const ProviderInfo& GetInfo() const = 0;

        /**
         * English: Get provider statistics
         * 한글: 공급자 통계 조회
         */
        virtual ProviderStats GetStats() const = 0;

        /**
         * English: Get last error message
         * 한글: 마지막 에러 메시지 조회
         */
        virtual const char* GetLastError() const = 0;
    };

    // =============================================================================  
    // English: Factory Functions
    // 한글: 팩토리 함수
    // =============================================================================  

    /**
     * English: Create AsyncIOProvider with automatic platform selection
     * 한글: 플랫폼 자동 선택으로 AsyncIOProvider 생성
     *
     * Fallback chains:
     * - Windows 8+: RIO -> IOCP -> nullptr
     * - Windows 7-: IOCP -> nullptr
     * - Linux 5.1+: io_uring -> epoll -> nullptr
     * - Linux 4.x: epoll -> nullptr
     * - macOS: kqueue -> nullptr
     *
     * @return Platform-appropriate provider or nullptr
     */
    std::unique_ptr<AsyncIOProvider> CreateAsyncIOProvider();

    /**
     * English: Create AsyncIOProvider with explicit platform hint
     * 한글: 명시적 플랫폼 힌트로 AsyncIOProvider 생성
     * @param platformHint Platform name ("IOCP", "RIO", "io_uring", "epoll", etc)
     * @return Specified provider or nullptr (not supported)
     */
    std::unique_ptr<AsyncIOProvider> CreateAsyncIOProvider(
        const char* platformHint
    );

    /**
     * English: Check if a platform is supported
     * 한글: 플랫폼 지원 여부 확인
     * @param platformHint Platform name
     * @return true if supported
     */
    bool IsPlatformSupported(const char* platformHint);

    /**
     * English: Get list of all supported platforms
     * 한글: 지원하는 모든 플랫폼 목록 조회
     * @param outCount Output: number of supported platforms
     * @return Array of platform name strings
     */
    const char** GetSupportedPlatforms(size_t& outCount);

    /**
     * English: Get current platform type at runtime
     * 한글: 런타임에 현재 플랫폼 타입 조회
     * @return Detected platform type
     */
    PlatformType GetCurrentPlatform();

    /**
     * English: Get detailed platform information
     * 한글: 상세 플랫폼 정보 조회
     * @return Platform information structure
     */
    PlatformInfo GetPlatformInfo();

}  // namespace AsyncIO
}  // namespace Network
