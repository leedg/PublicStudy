#pragma once
// encoding: UTF-8

// Unified async I/O provider interface for all platforms
//
// =============================================================================
// =============================================================================
//
// This is a LOW-LEVEL abstraction for cross-platform async I/O.
// Use this when:
//   - Building multi-platform network libraries
//   - Need Session-independent I/O operations
//   - Want to switch between IOCP/RIO/epoll/io_uring dynamically
//
// Don't use this when:
//   - Building Windows-only servers (use IOCPNetworkEngine directly)
//   - Need Session lifecycle management (use IOCPNetworkEngine + Session)
//
//
// =============================================================================

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
// Windows socket handle type
using SocketHandle = SOCKET;
// Windows OS error type
using OSError = DWORD;
#else
#include <sys/socket.h>
// POSIX socket handle type (file descriptor)
using SocketHandle = int;
// POSIX OS error type
using OSError = int;
#endif

namespace Network
{
namespace AsyncIO
{
// =============================================================================
// Type Definitions
// =============================================================================

// User-defined context for async operations
using RequestContext = uint64_t;

// Completion callback function type
using CompletionCallback =
	std::function<void(const struct CompletionEntry &, void *userData)>;

// =============================================================================
// Enumerations
// =============================================================================

// Async I/O operation types
enum class AsyncIOType : uint8_t
{
	// Send operation
	Send,

	// Receive operation
	Recv,

	// Accept connection (listener)
	Accept,

	// Connect request (client)
	Connect,

	// Timeout (internal use)
	Timeout,

	// Error (internal use)
	Error,
};

// Platform types (backend implementations)
// Note: These represent AsyncIO BACKEND implementations, NOT OS platforms
// - Windows: Default = IOCP, High-Performance = RIO
// - Linux: Default = epoll, High-Performance = IOUring
// - macOS: Always = Kqueue
enum class PlatformType : uint8_t
{
	// Windows IOCP (stable, all Windows versions)
	IOCP,

	// Windows Registered I/O (high-performance, Windows 8+)
	RIO,

	// Linux epoll (stable, all Linux)
	Epoll,

	// Linux io_uring (high-performance, kernel 5.1+)
	IOUring,

	// macOS kqueue (standard)
	Kqueue,
};

// Error codes for async I/O operations
enum class AsyncIOError : int32_t
{
	// Operation completed successfully
	Success = 0,

	// Provider not initialized
	NotInitialized = -1,

	// Invalid socket handle
	InvalidSocket = -2,

	// Operation is pending
	OperationPending = -3,

	// Operation failed
	OperationFailed = -4,

	// Invalid buffer
	InvalidBuffer = -5,

	// No resources available
	NoResources = -6,

	// Operation timed out
	Timeout = -7,

	// Platform not supported
	PlatformNotSupported = -8,

	// Already initialized
	AlreadyInitialized = -9,

	// Invalid parameter
	InvalidParameter = -10,

	// Memory allocation failed
	AllocationFailed = -11,

	// Resource exhausted
	ResourceExhausted = -12,
};

// Buffer registration policy
enum class BufferPolicy : uint8_t
{
	// Buffer can be reused for multiple operations
	Reuse,

	// Buffer is used once then freed
	SingleUse,

	// Buffer is from a pool
	Pooled,
};

// =============================================================================
// Structures
// =============================================================================

// Completion entry from I/O completion
struct CompletionEntry
{
	// Request context (user-defined ID)
	RequestContext mContext;

	// Type of operation (Send/Recv/etc)
	AsyncIOType mType;

	// Bytes transferred or error code
	int32_t mResult;

	// System error code (0 = success)
	OSError mOsError;

	// Completion time in nanoseconds (optional)
	uint64_t mCompletionTime;
};

// Send/Receive buffer structure
struct IOBuffer
{
	// Buffer pointer
	void *mData;

	// Buffer size
	size_t mSize;

	// Offset (can be used instead of RIO BufferId)
	size_t mOffset;
};

// Provider information structure
struct ProviderInfo
{
	// Platform type (backend implementation)
	PlatformType mPlatformType;

	// Human-readable name ("IOCP", "RIO", "io_uring", etc)
	const char *mName;

	// Capabilities flags (supported features)
	uint32_t mCapabilities;

	// Maximum queue depth
	size_t mMaxQueueDepth;

	// Maximum concurrent requests
	size_t mMaxConcurrentReq;

	// Buffer pre-registration support
	bool mSupportsBufferReg;

	// Batch processing support
	bool mSupportsBatching;

	// Zero-copy support
	bool mSupportsZeroCopy;
};

// Provider statistics structure
struct ProviderStats
{
	// Total number of requests
	uint64_t mTotalRequests;

	// Total number of completions
	uint64_t mTotalCompletions;

	// Number of pending requests
	uint64_t mPendingRequests;

	// Average latency in nanoseconds
	uint64_t mAvgLatency;

	// P99 latency
	double mP99Latency;

	// Error count
	uint64_t mErrorCount;
};

// Platform information (for detection)
struct PlatformInfo
{
	// Detected platform type
	PlatformType mPlatformType;

	// OS major version
	uint32_t mMajorVersion;

	// OS minor version
	uint32_t mMinorVersion;

	// Human-readable platform name
	const char *mPlatformName;

	// Windows RIO support
	bool mSupportRIO;

	// Linux io_uring support
	bool mSupportIOUring;

	// macOS kqueue support
	bool mSupportKqueue;
};

// Buffer registration result
struct BufferRegistration
{
	// Buffer ID (for future reference)
	int64_t mBufferId;

	// Registration successful?
	bool mSuccess;

	// Error code if failed
	int32_t mErrorCode;
};

// =============================================================================
// Abstract Interface: AsyncIOProvider
// =============================================================================

class AsyncIOProvider
{
  public:
	// Virtual destructor
	virtual ~AsyncIOProvider() = default;

	// =====================================================================
	// Lifecycle Management
	// =====================================================================

	/**
	 * Initialize async I/O provider
	 * @param queueDepth Queue depth for requests/completions (32-4096)
	 * @param maxConcurrent Maximum concurrent requests
	 * @return Error code (Success if initialization succeeded)
	 */
	virtual AsyncIOError Initialize(size_t queueDepth,
									size_t maxConcurrent) = 0;

	/**
	 * Shutdown async I/O provider
	 */
	virtual void Shutdown() = 0;

	/**
	 * Check if provider is initialized
	 * @return true if initialized
	 */
	virtual bool IsInitialized() const = 0;

	// =====================================================================
	// Buffer Management
	// =====================================================================

	/**
	 * Register a buffer for optimized I/O (RIO/io_uring specific)
	 * @param ptr Buffer pointer
	 * @param size Buffer size
	 * @return Buffer ID (>= 0 success, < 0 error)
	 *
	 * Note: Only meaningful for RIO/io_uring (IOCP returns no-op)
	 */
	virtual int64_t RegisterBuffer(const void *ptr, size_t size) = 0;

	/**
	 * Unregister a previously registered buffer
	 * @param bufferId Buffer ID from RegisterBuffer
	 * @return Error code
	 */
	virtual AsyncIOError UnregisterBuffer(int64_t bufferId) = 0;

	// =====================================================================
	// Socket Association
	// =====================================================================

	/**
	 * Associate a socket with the I/O provider for async operations
	 *
	 * This MUST be called after accept() and before any async I/O
	 * (SendAsync/RecvAsync/PostRecv) on the socket.
	 *
	 *
	 * Platform behavior:
	 * - IOCP: CreateIoCompletionPort(socket, completionPort, context, 0)
	 * - epoll: epoll_ctl(EPOLL_CTL_ADD)
	 * - kqueue: kevent() with EV_ADD
	 * - RIO/io_uring: Platform-specific registration
	 *
	 * @param socket Socket handle to associate
	 * @param context Request context (typically ConnectionId)
	 * @return Error code (Success if association succeeded)
	 */
	virtual AsyncIOError AssociateSocket(SocketHandle socket,
										 RequestContext context) = 0;

	// =====================================================================
	// Async I/O Requests
	// =====================================================================

	/**
	 * Asynchronous send operation
	 * @param socket Socket handle
	 * @param buffer Send buffer
	 * @param size Send size
	 * @param context Request ID (returned in completion)
	 * @param flags Platform-specific flags
	 * @return Error code
	 *
	 * Note: Behavior varies by platform
	 * - IOCP: Immediate execution (flags ignored)
	 * - RIO: With RIO_MSG_DEFER, waits for batch processing
	 * - io_uring: Automatic batch processing
	 */
	virtual AsyncIOError SendAsync(SocketHandle socket, const void *buffer,
									   size_t size, RequestContext context,
									   uint32_t flags = 0) = 0;

	/**
	 * Asynchronous receive operation
	 * @param socket Socket handle
	 * @param buffer Receive buffer
	 * @param size Buffer size
	 * @param context Request ID
	 * @param flags Platform-specific flags
	 * @return Error code
	 */
	virtual AsyncIOError RecvAsync(SocketHandle socket, void *buffer,
									   size_t size, RequestContext context,
									   uint32_t flags = 0) = 0;

	/**
	 * Flush pending requests (batch execution)
	 *
	 * - IOCP: no-op (SendAsync executes immediately)
	 * - RIO: Commits deferred sends/recvs to kernel
	 * - io_uring: Submits all SQ entries to kernel
	 *
	 * @return Error code
	 */
	virtual AsyncIOError FlushRequests() = 0;

	// =====================================================================
	// Completion Processing
	// =====================================================================

	/**
	 * Process completed operations (non-blocking or with timeout)
	 * @param entries Output array of completion entries
	 * @param maxEntries Array size
	 * @param timeoutMs Timeout:
	 *     - 0: Non-blocking (immediate return)
	 *     - >0: Wait in milliseconds
	 *     - -1: Infinite wait
	 * @return Number of completions processed (negative = error)
	 */
	virtual int ProcessCompletions(CompletionEntry *entries, size_t maxEntries,
									   int timeoutMs = 0) = 0;

	// =====================================================================
	// Information & Statistics
	// =====================================================================

	/**
	 * Get provider information
	 */
	virtual const ProviderInfo &GetInfo() const = 0;

	/**
	 * Get provider statistics
	 */
	virtual ProviderStats GetStats() const = 0;

	/**
	 * Get last error message
	 */
	virtual const char *GetLastError() const = 0;
};

// =============================================================================
// Factory Functions
// =============================================================================

/**
 * Create AsyncIOProvider with automatic platform selection
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
 * Create AsyncIOProvider with explicit platform hint
 * @param platformHint Platform name ("IOCP", "RIO", "io_uring", "epoll", etc)
 * @return Specified provider or nullptr (not supported)
 */
std::unique_ptr<AsyncIOProvider>
CreateAsyncIOProvider(const char *platformHint);

/**
 * Check if a platform is supported
 * @param platformHint Platform name
 * @return true if supported
 */
bool IsPlatformSupported(const char *platformHint);

/**
 * Get list of all supported platforms
 * @param outCount Output: number of supported platforms
 * @return Array of platform name strings
 */
const char **GetSupportedPlatforms(size_t &outCount);

/**
 * Get current platform type at runtime
 * @return Detected platform type
 */
PlatformType GetCurrentPlatform();

/**
 * Get detailed platform information
 * @return Platform information structure
 */
PlatformInfo GetPlatformInfo();

} // namespace AsyncIO
} // namespace Network
