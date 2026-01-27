#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <functional>

#ifdef _WIN32
    #include <winsock2.h>
    #include <mswsock.h>
    using SocketHandle = SOCKET;
    using OSError = DWORD;
#else
    #include <sys/socket.h>
    using SocketHandle = int;
    using OSError = int;
#endif

namespace Network::AsyncIO
{
    // =============================================================================
    // Type Definitions
    // =============================================================================

    // User-defined context for async operations
    using RequestContext = uint64_t;

    // Completion callback function type
    using CompletionCallback = std::function<void(const struct CompletionEntry&, void* userData)>;

    // =============================================================================
    // Enumerations
    // =============================================================================

    // Async I/O operation types
    enum class AsyncIOType : uint8_t
    {
        Send,       // Send operation
        Recv,       // Receive operation
        Accept,     // Accept connection (listener)
        Connect,    // Connect request (client)
        Timeout,    // Timeout (internal use)
        Error,      // Error (internal use)
    };

    // Platform types (backend implementations)
    // Note: These represent AsyncIO BACKEND implementations, NOT OS platforms
    // - On Windows: Default = IOCP, High-Performance = RIO
    // - On Linux: Default = epoll, High-Performance = IOUring
    // - On macOS: Always = Kqueue
    enum class PlatformType : uint8_t
    {
        IOCP,       // Windows IOCP (stable baseline)
        RIO,        // Windows Registered I/O (Windows 8+, high-perf)
        Epoll,      // Linux epoll (stable baseline)
        IOUring,    // Linux io_uring (kernel 5.1+, high-perf)
        Kqueue,     // macOS kqueue (standard)
    };

    // Error codes
    enum class AsyncIOError : int32_t
    {
        Success = 0,
        InvalidSocket = -1,
        InvalidBuffer = -2,
        AllocationFailed = -3,
        OperationFailed = -4,
        Timeout = -5,
        PlatformNotSupported = -6,
        InvalidParameter = -7,
        ResourceExhausted = -8,
    };

    // Buffer registration policy
    enum class BufferPolicy : uint8_t
    {
        Reuse,          // Buffer can be reused for multiple operations
        SingleUse,      // Buffer is used once then freed
        Pooled,         // Buffer is from a pool
    };

    // =============================================================================
    // Structures
    // =============================================================================

    // Completion entry from I/O completion
    struct CompletionEntry
    {
        AsyncIOType operationType;      // Type of operation (Send/Recv/etc)
        uint32_t bytesTransferred;      // Bytes transferred in operation
        int32_t errorCode;              // Error code (0 = success)
        void* userData;                 // User-provided context pointer
        uint64_t internalHandle;        // Internal platform-specific handle
    };

    // Buffer registration result
    struct BufferRegistration
    {
        int64_t bufferId;               // Buffer ID (for future reference)
        bool success;                   // Registration successful?
        int32_t errorCode;              // Error code if failed
    };

    // Platform information
    struct PlatformInfo
    {
        PlatformType platformType;      // Detected platform
        uint32_t majorVersion;          // OS major version
        uint32_t minorVersion;          // OS minor version
        const char* platformName;       // Human-readable platform name
        bool supportRIO;                // Windows: RIO support
        bool supportIOUring;            // Linux: io_uring support
        bool supportKqueue;             // macOS: kqueue support
    };

    // =============================================================================
    // Abstract Interface: AsyncIOProvider
    // =============================================================================

    class AsyncIOProvider
    {
    public:
        virtual ~AsyncIOProvider() = default;

        // =====================================================================
        // Initialization & Configuration
        // =====================================================================

        /**
         * Initialize the provider with given parameters
         * @param maxConcurrentOps Maximum concurrent operations to support
         * @return true if initialization successful
         */
        virtual bool Initialize(uint32_t maxConcurrentOps = 10000) = 0;

        /**
         * Shutdown the provider and release resources
         */
        virtual void Shutdown() = 0;

        /**
         * Get platform information
         * @return Platform information structure
         */
        virtual PlatformInfo GetPlatformInfo() const = 0;

        /**
         * Check if a feature is supported
         * @param featureName Name of the feature to check
         * @return true if feature is supported
         */
        virtual bool SupportsFeature(const char* featureName) const = 0;

        // =====================================================================
        // Socket Management
        // =====================================================================

        /**
         * Register a socket with this provider
         * @param socket Socket handle to register
         * @return true if registration successful
         */
        virtual bool RegisterSocket(SocketHandle socket) = 0;

        /**
         * Unregister a socket from this provider
         * @param socket Socket handle to unregister
         * @return true if unregistration successful
         */
        virtual bool UnregisterSocket(SocketHandle socket) = 0;

        // =====================================================================
        // Async I/O Operations
        // =====================================================================

        /**
         * Asynchronous send operation
         * @param socket Socket to send on
         * @param data Buffer containing data to send
         * @param size Number of bytes to send
         * @param userData User context pointer (passed to callback)
         * @param flags Platform-specific flags
         * @param callback Callback function when operation completes
         * @return true if operation initiated successfully
         */
        virtual bool SendAsync(
            SocketHandle socket,
            const void* data,
            uint32_t size,
            void* userData,
            uint32_t flags,
            CompletionCallback callback
        ) = 0;

        /**
         * Asynchronous send on registered buffer (RIO/io_uring optimization)
         * @param socket Socket to send on
         * @param registeredBufferId ID from RegisterBuffer()
         * @param offset Offset within the buffer
         * @param length Number of bytes to send
         * @param userData User context pointer
         * @param flags Platform-specific flags
         * @param callback Callback function when operation completes
         * @return true if operation initiated successfully
         */
        virtual bool SendAsyncRegistered(
            SocketHandle socket,
            int64_t registeredBufferId,
            uint32_t offset,
            uint32_t length,
            void* userData,
            uint32_t flags,
            CompletionCallback callback
        ) = 0;

        /**
         * Asynchronous receive operation
         * @param socket Socket to receive on
         * @param buffer Buffer to store received data
         * @param size Maximum bytes to receive
         * @param userData User context pointer
         * @param flags Platform-specific flags
         * @param callback Callback function when operation completes
         * @return true if operation initiated successfully
         */
        virtual bool RecvAsync(
            SocketHandle socket,
            void* buffer,
            uint32_t size,
            void* userData,
            uint32_t flags,
            CompletionCallback callback
        ) = 0;

        /**
         * Asynchronous receive on registered buffer
         * @param socket Socket to receive on
         * @param registeredBufferId ID from RegisterBuffer()
         * @param offset Offset within the buffer
         * @param length Maximum bytes to receive
         * @param userData User context pointer
         * @param flags Platform-specific flags
         * @param callback Callback function when operation completes
         * @return true if operation initiated successfully
         */
        virtual bool RecvAsyncRegistered(
            SocketHandle socket,
            int64_t registeredBufferId,
            uint32_t offset,
            uint32_t length,
            void* userData,
            uint32_t flags,
            CompletionCallback callback
        ) = 0;

        // =====================================================================
        // Buffer Management
        // =====================================================================

        /**
         * Register a buffer for optimized I/O (RIO/io_uring specific)
         * @param buffer Pointer to buffer
         * @param size Size of buffer in bytes
         * @param policy Buffer usage policy
         * @return Buffer registration result
         */
        virtual BufferRegistration RegisterBuffer(
            const void* buffer,
            uint32_t size,
            BufferPolicy policy = BufferPolicy::Reuse
        ) = 0;

        /**
         * Unregister a previously registered buffer
         * @param bufferId ID returned from RegisterBuffer()
         * @return true if unregistration successful
         */
        virtual bool UnregisterBuffer(int64_t bufferId) = 0;

        /**
         * Get number of registered buffers currently in use
         * @return Number of active buffer registrations
         */
        virtual uint32_t GetRegisteredBufferCount() const = 0;

        // =====================================================================
        // Completion Processing
        // =====================================================================

        /**
         * Process pending completions from I/O queue
         * @param entries Array to store completion entries
         * @param maxCount Maximum number of entries to retrieve
         * @param timeoutMs Timeout in milliseconds (0 = non-blocking, ~0 = infinite)
         * @return Number of completion entries processed (0 = timeout)
         */
        virtual uint32_t ProcessCompletions(
            CompletionEntry* entries,
            uint32_t maxCount,
            uint32_t timeoutMs
        ) = 0;

        // =====================================================================
        // Statistics & Monitoring
        // =====================================================================

        /**
         * Get current pending operation count
         * @return Number of pending operations
         */
        virtual uint32_t GetPendingOperationCount() const = 0;

        /**
         * Get cumulative operation statistics
         * @param outStats Pointer to stats structure (format implementation-specific)
         * @return true if statistics retrieved
         */
        virtual bool GetStatistics(void* outStats) const = 0;

        /**
         * Reset statistics counters
         */
        virtual void ResetStatistics() = 0;
    };

    // =============================================================================
    // Factory Functions
    // =============================================================================

    /**
     * Create an AsyncIOProvider instance for the current platform
     * @param preferHighPerformance true to prefer high-performance backends (RIO, io_uring)
     * @return Unique pointer to created provider, or nullptr if creation failed
     */
    std::unique_ptr<AsyncIOProvider> CreateAsyncIOProvider(bool preferHighPerformance = true);

    /**
     * Create an AsyncIOProvider instance for a specific platform
     * @param platformType Platform to create provider for
     * @return Unique pointer to created provider, or nullptr if not supported
     */
    std::unique_ptr<AsyncIOProvider> CreateAsyncIOProviderForPlatform(PlatformType platformType);

    /**
     * Get the current platform type at runtime
     * @return Detected platform type
     */
    PlatformType GetCurrentPlatform();

    /**
     * Get detailed platform information
     * @return Platform information structure
     */
    PlatformInfo GetPlatformInfo();

}  // namespace Network::AsyncIO
