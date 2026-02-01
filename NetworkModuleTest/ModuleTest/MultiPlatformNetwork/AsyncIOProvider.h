#pragma once

// English: Unified async I/O provider interface for all platforms
// ?쒓?: 紐⑤뱺 ?뚮옯?쇱쓽 鍮꾨룞湲?I/O瑜??듭씪?섎뒗 ?명꽣?섏씠??

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
    // ?쒓?: Windows ?뚯폆 ?몃뱾 ???
    using SocketHandle = SOCKET;
    // English: Windows OS error type
    // ?쒓?: Windows OS ?먮윭 ???
    using OSError = DWORD;
#else
    #include <sys/socket.h>
    // English: POSIX socket handle type (file descriptor)
    // ?쒓?: POSIX ?뚯폆 ?몃뱾 ???(?뚯씪 ?붿뒪?щ┰??
    using SocketHandle = int;
    // English: POSIX OS error type
    // ?쒓?: POSIX OS ?먮윭 ???
    using OSError = int;
#endif

namespace Network {
namespace AsyncIO {
    // =============================================================================
    // English: Type Definitions
    // ?쒓?: ????뺤쓽
    // =============================================================================

    // English: User-defined context for async operations
    // ?쒓?: 鍮꾨룞湲??묒뾽???ъ슜???뺤쓽 而⑦뀓?ㅽ듃
    using RequestContext = uint64_t;

    // English: Completion callback function type
    // ?쒓?: ?꾨즺 肄쒕갚 ?⑥닔 ???
    using CompletionCallback = std::function<void(const struct CompletionEntry&, void* userData)>;

    // =============================================================================
    // English: Enumerations
    // ?쒓?: ?닿굅??
    // =============================================================================

    // English: Async I/O operation types
    // ?쒓?: 鍮꾨룞湲?I/O ?묒뾽 ???
    enum class AsyncIOType : uint8_t
    {
        // English: Send operation
        // ?쒓?: ?≪떊 ?묒뾽
        Send,

        // English: Receive operation
        // ?쒓?: ?섏떊 ?묒뾽
        Recv,

        // English: Accept connection (listener)
        // ?쒓?: ?곌껐 ?섎씫 (由ъ뒪??
        Accept,

        // English: Connect request (client)
        // ?쒓?: ?곌껐 ?붿껌 (?대씪?댁뼵??
        Connect,

        // English: Timeout (internal use)
        // ?쒓?: ??꾩븘??(?대? ?ъ슜)
        Timeout,

        // English: Error (internal use)
        // ?쒓?: ?먮윭 (?대? ?ъ슜)
        Error,
    };

    // English: Platform types (backend implementations)
    // ?쒓?: ?뚮옯?????(諛깆뿏??援ы쁽)
    // Note: These represent AsyncIO BACKEND implementations, NOT OS platforms
    // 李멸퀬: OS ?뚮옯?쇱씠 ?꾨땲??AsyncIO 諛깆뿏??援ы쁽???섑??낅땲??
    // - Windows: Default = IOCP, High-Performance = RIO
    // - Linux: Default = epoll, High-Performance = IOUring
    // - macOS: Always = Kqueue
    enum class PlatformType : uint8_t
    {
        // English: Windows IOCP (stable, all Windows versions)
        // ?쒓?: Windows IOCP (?덉젙?? 紐⑤뱺 Windows 踰꾩쟾)
        IOCP,

        // English: Windows Registered I/O (high-performance, Windows 8+)
        // ?쒓?: Windows ?깅줉 I/O (怨좎꽦?? Windows 8+)
        RIO,

        // English: Linux epoll (stable, all Linux)
        // ?쒓?: Linux epoll (?덉젙?? 紐⑤뱺 Linux)
        Epoll,

        // English: Linux io_uring (high-performance, kernel 5.1+)
        // ?쒓?: Linux io_uring (怨좎꽦?? 而ㅻ꼸 5.1+)
        IOUring,

        // English: macOS kqueue (standard)
        // ?쒓?: macOS kqueue (?쒖?)
        Kqueue,
    };

    // English: Error codes for async I/O operations
    // ?쒓?: 鍮꾨룞湲?I/O ?묒뾽???먮윭 肄붾뱶
    enum class AsyncIOError : int32_t
    {
        // English: Operation completed successfully
        // ?쒓?: ?묒뾽???깃났?곸쑝濡??꾨즺??
        Success = 0,

        // English: Provider not initialized
        // ?쒓?: 怨듦툒?먭? 珥덇린?붾릺吏 ?딆쓬
        NotInitialized = -1,

        // English: Invalid socket handle
        // ?쒓?: ?섎せ???뚯폆 ?몃뱾
        InvalidSocket = -2,

        // English: Operation is pending
        // ?쒓?: ?묒뾽???湲?以?
        OperationPending = -3,

        // English: Operation failed
        // ?쒓?: ?묒뾽???ㅽ뙣??
        OperationFailed = -4,

        // English: Invalid buffer
        // ?쒓?: ?섎せ??踰꾪띁
        InvalidBuffer = -5,

        // English: No resources available
        // ?쒓?: ?ъ슜 媛?ν븳 由ъ냼???놁쓬
        NoResources = -6,

        // English: Operation timed out
        // ?쒓?: ?묒뾽????꾩븘?껊맖
        Timeout = -7,

        // English: Platform not supported
        // ?쒓?: ?뚮옯?쇱씠 吏?먮릺吏 ?딆쓬
        PlatformNotSupported = -8,

        // English: Already initialized
        // ?쒓?: ?대? 珥덇린?붾맖
        AlreadyInitialized = -9,

        // English: Invalid parameter
        // ?쒓?: ?섎せ??留ㅺ컻蹂??
        InvalidParameter = -10,

        // English: Memory allocation failed
        // ?쒓?: 硫붾え由??좊떦 ?ㅽ뙣
        AllocationFailed = -11,

        // English: Resource exhausted
        // ?쒓?: 由ъ냼??怨좉컝
        ResourceExhausted = -12,
    };

    // English: Buffer registration policy
    // ?쒓?: 踰꾪띁 ?깅줉 ?뺤콉
    enum class BufferPolicy : uint8_t
    {
        // English: Buffer can be reused for multiple operations
        // ?쒓?: 踰꾪띁瑜??щ윭 ?묒뾽???ъ궗??媛??
        Reuse,

        // English: Buffer is used once then freed
        // ?쒓?: 踰꾪띁瑜???踰??ъ슜 ???댁젣
        SingleUse,

        // English: Buffer is from a pool
        // ?쒓?: 踰꾪띁媛 ??먯꽌 ?쒓났??
        Pooled,
    };

    // =============================================================================
    // English: Structures
    // ?쒓?: 援ъ“泥?
    // =============================================================================

    // English: Completion entry from I/O completion
    // ?쒓?: I/O ?꾨즺 ??ぉ
    struct CompletionEntry
    {
        // English: Request context (user-defined ID)
        // ?쒓?: ?붿껌 而⑦뀓?ㅽ듃 (?ъ슜???뺤쓽 ID)
        RequestContext mContext;

        // English: Type of operation (Send/Recv/etc)
        // ?쒓?: ?묒뾽 ???(Send/Recv ??
        AsyncIOType mType;

        // English: Bytes transferred or error code
        // ?쒓?: ?꾩넚??諛붿씠?????먮뒗 ?먮윭 肄붾뱶
        int32_t mResult;

        // English: System error code (0 = success)
        // ?쒓?: ?쒖뒪???먮윭 肄붾뱶 (0 = ?깃났)
        OSError mOsError;

        // English: Completion time in nanoseconds (optional)
        // ?쒓?: ?꾨즺 ?쒓컙 (?섎끂珥? ?좏깮?ы빆)
        uint64_t mCompletionTime;
    };

    // English: Send/Receive buffer structure
    // ?쒓?: ?≪닔??踰꾪띁 援ъ“泥?
    struct IOBuffer
    {
        // English: Buffer pointer
        // ?쒓?: 踰꾪띁 ?ъ씤??
        void* mData;

        // English: Buffer size
        // ?쒓?: 踰꾪띁 ?ш린
        size_t mSize;

        // English: Offset (can be used instead of RIO BufferId)
        // ?쒓?: ?ㅽ봽??(RIO BufferId ????ъ슜 媛??
        size_t mOffset;
    };

    // English: Provider information structure
    // ?쒓?: 怨듦툒???뺣낫 援ъ“泥?
    struct ProviderInfo
    {
        // English: Platform type (backend implementation)
        // ?쒓?: ?뚮옯?????(諛깆뿏??援ы쁽)
        PlatformType mPlatformType;

        // English: Human-readable name ("IOCP", "RIO", "io_uring", etc)
        // ?쒓?: ?щ엺???쎌쓣 ???덈뒗 ?대쫫 ("IOCP", "RIO", "io_uring" ??
        const char* mName;

        // English: Capabilities flags (supported features)
        // ?쒓?: 湲곕뒫 ?뚮옒洹?(吏??湲곕뒫)
        uint32_t mCapabilities;

        // English: Maximum queue depth
        // ?쒓?: 理쒕? ??源딆씠
        size_t mMaxQueueDepth;

        // English: Maximum concurrent requests
        // ?쒓?: 理쒕? ?숈떆 ?붿껌
        size_t mMaxConcurrentReq;

        // English: Buffer pre-registration support
        // ?쒓?: 踰꾪띁 ?ъ쟾 ?깅줉 吏??
        bool mSupportsBufferReg;

        // English: Batch processing support
        // ?쒓?: 諛곗튂 泥섎━ 吏??
        bool mSupportsBatching;

        // English: Zero-copy support
        // ?쒓?: Zero-copy 吏??
        bool mSupportsZeroCopy;
    };

    // English: Provider statistics structure
    // ?쒓?: 怨듦툒???듦퀎 援ъ“泥?
    struct ProviderStats
    {
        // English: Total number of requests
        // ?쒓?: ?꾩껜 ?붿껌 ??
        uint64_t mTotalRequests;

        // English: Total number of completions
        // ?쒓?: ?꾩껜 ?꾨즺 ??
        uint64_t mTotalCompletions;

        // English: Number of pending requests
        // ?쒓?: ?湲?以묒씤 ?붿껌 ??
        uint64_t mPendingRequests;

        // English: Average latency in nanoseconds
        // ?쒓?: ?됯퇏 ?덉씠?댁떆 (?섎끂珥?
        uint64_t mAvgLatency;

        // English: P99 latency
        // ?쒓?: P99 ?덉씠?댁떆
        double mP99Latency;

        // English: Error count
        // ?쒓?: ?먮윭 ??
        uint64_t mErrorCount;
    };

    // English: Platform information (for detection)
    // ?쒓?: ?뚮옯???뺣낫 (媛먯???
    struct PlatformInfo
    {
        // English: Detected platform type
        // ?쒓?: 媛먯????뚮옯?????
        PlatformType mPlatformType;

        // English: OS major version
        // ?쒓?: OS 二?踰꾩쟾
        uint32_t mMajorVersion;

        // English: OS minor version
        // ?쒓?: OS 遺 踰꾩쟾
        uint32_t mMinorVersion;

        // English: Human-readable platform name
        // ?쒓?: ?щ엺???쎌쓣 ???덈뒗 ?뚮옯???대쫫
        const char* mPlatformName;

        // English: Windows RIO support
        // ?쒓?: Windows RIO 吏??
        bool mSupportRIO;

        // English: Linux io_uring support
        // ?쒓?: Linux io_uring 吏??
        bool mSupportIOUring;

        // English: macOS kqueue support
        // ?쒓?: macOS kqueue 吏??
        bool mSupportKqueue;
    };

    // English: Buffer registration result
    // ?쒓?: 踰꾪띁 ?깅줉 寃곌낵
    struct BufferRegistration
    {
        // English: Buffer ID (for future reference)
        // ?쒓?: 踰꾪띁 ID (?ν썑 李몄“??
        int64_t mBufferId;

        // English: Registration successful?
        // ?쒓?: ?깅줉 ?깃났 ?щ?
        bool mSuccess;

        // English: Error code if failed
        // ?쒓?: ?ㅽ뙣 ???먮윭 肄붾뱶
        int32_t mErrorCode;
    };

    // =============================================================================
    // English: Abstract Interface: AsyncIOProvider
    // ?쒓?: 異붿긽 ?명꽣?섏씠?? AsyncIOProvider
    // =============================================================================

    class AsyncIOProvider
    {
    public:
        // English: Virtual destructor
        // ?쒓?: 媛???뚮㈇??
        virtual ~AsyncIOProvider() = default;

        // =====================================================================
        // English: Lifecycle Management
        // ?쒓?: ?앸챸二쇨린 愿由?
        // =====================================================================

        /**
         * English: Initialize the async I/O provider
         * ?쒓?: 鍮꾨룞湲?I/O 怨듦툒??珥덇린??
         * @param queueDepth Queue depth for requests/completions (32-4096)
         * @param maxConcurrent Maximum concurrent requests
         * @return Error code (Success if initialization succeeded)
         */
        virtual AsyncIOError Initialize(
            size_t queueDepth,
            size_t maxConcurrent
        ) = 0;

        /**
         * English: Shutdown the async I/O provider
         * ?쒓?: 鍮꾨룞湲?I/O 怨듦툒??醫낅즺
         */
        virtual void Shutdown() = 0;

        /**
         * English: Check if provider is initialized
         * ?쒓?: 怨듦툒??珥덇린???щ? ?뺤씤
         * @return true if initialized
         */
        virtual bool IsInitialized() const = 0;

        // =====================================================================
        // English: Buffer Management
        // ?쒓?: 踰꾪띁 愿由?
        // =====================================================================

        /**
         * English: Register a buffer for optimized I/O (RIO/io_uring specific)
         * ?쒓?: 理쒖쟻?붾맂 I/O??踰꾪띁 ?ъ쟾 ?깅줉 (RIO/io_uring ?꾩슜)
         * @param ptr Buffer pointer
         * @param size Buffer size
         * @return Buffer ID (>= 0 success, < 0 error)
         *
         * Note: Only meaningful for RIO/io_uring (IOCP returns no-op)
         * 李멸퀬: RIO/io_uring?먯꽌留??섎? ?덉쓬 (IOCP??no-op)
         */
        virtual int64_t RegisterBuffer(const void* ptr, size_t size) = 0;

        /**
         * English: Unregister a previously registered buffer
         * ?쒓?: ?댁쟾???깅줉??踰꾪띁 ?깅줉 ?댁젣
         * @param bufferId Buffer ID from RegisterBuffer
         * @return Error code
         */
        virtual AsyncIOError UnregisterBuffer(int64_t bufferId) = 0;

        // =====================================================================
        // English: Async I/O Requests
        // ?쒓?: 鍮꾨룞湲?I/O ?붿껌
        // =====================================================================

        /**
         * English: Asynchronous send operation
         * ?쒓?: 鍮꾨룞湲??≪떊 ?묒뾽
         * @param socket Socket handle
         * @param buffer Send buffer
         * @param size Send size
         * @param context Request ID (returned in completion)
         * @param flags Platform-specific flags
         * @return Error code
         *
         * Note: Behavior varies by platform
         * 李멸퀬: ?뚮옯?쇰쭏???숈옉???ㅻ쫫
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
         * ?쒓?: 鍮꾨룞湲??섏떊 ?묒뾽
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
         * ?쒓?: ?湲?以묒씤 ?붿껌 ?쇨큵 ?ㅽ뻾 (諛곗튂 泥섎━)
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
        // ?쒓?: ?꾨즺 泥섎━
        // =====================================================================

        /**
         * English: Process completed operations (non-blocking or with timeout)
         * ?쒓?: ?꾨즺???묒뾽 泥섎━ (?쇰툝濡쒗궧 ?먮뒗 ??꾩븘??
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
        // ?쒓?: ?뺣낫 諛??듦퀎
        // =====================================================================

        /**
         * English: Get provider information
         * ?쒓?: 怨듦툒???뺣낫 議고쉶
         */
        virtual const ProviderInfo& GetInfo() const = 0;

        /**
         * English: Get provider statistics
         * ?쒓?: 怨듦툒???듦퀎 議고쉶
         */
        virtual ProviderStats GetStats() const = 0;

        /**
         * English: Get last error message
         * ?쒓?: 留덉?留??먮윭 硫붿떆吏 議고쉶
         */
        virtual const char* GetLastError() const = 0;
    };

    // =============================================================================
    // English: Factory Functions
    // ?쒓?: ?⑺넗由??⑥닔
    // =============================================================================

    /**
     * English: Create AsyncIOProvider with automatic platform selection
     * ?쒓?: ?뚮옯???먮룞 ?좏깮?쇰줈 AsyncIOProvider ?앹꽦
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
     * ?쒓?: 紐낆떆???뚮옯???뚰듃濡?AsyncIOProvider ?앹꽦
     * @param platformHint Platform name ("IOCP", "RIO", "io_uring", "epoll", etc)
     * @return Specified provider or nullptr (not supported)
     */
    std::unique_ptr<AsyncIOProvider> CreateAsyncIOProvider(
        const char* platformHint
    );

    /**
     * English: Check if a platform is supported
     * ?쒓?: ?뚮옯??吏???щ? ?뺤씤
     * @param platformHint Platform name
     * @return true if supported
     */
    bool IsPlatformSupported(const char* platformHint);

    /**
     * English: Get list of all supported platforms
     * ?쒓?: 吏?먰븯??紐⑤뱺 ?뚮옯??紐⑸줉 議고쉶
     * @param outCount Output: number of supported platforms
     * @return Array of platform name strings
     */
    const char** GetSupportedPlatforms(size_t& outCount);

    /**
     * English: Get current platform type at runtime
     * ?쒓?: ?고??꾩뿉 ?꾩옱 ?뚮옯?????議고쉶
     * @return Detected platform type
     */
    PlatformType GetCurrentPlatform();

    /**
     * English: Get detailed platform information
     * ?쒓?: ?곸꽭 ?뚮옯???뺣낫 議고쉶
     * @return Platform information structure
     */
    PlatformInfo GetPlatformInfo();

}  // namespace AsyncIO
}  // namespace Network

