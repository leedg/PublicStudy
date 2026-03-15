#pragma once

// IOCP-based AsyncIOProvider implementation for Windows
//
// =============================================================================
// =============================================================================
//
// IocpAsyncIOProvider and IOCPNetworkEngine serve DIFFERENT purposes:
//
// IocpAsyncIOProvider:
//   - Low-level IOCP abstraction for AsyncIOProvider interface
//   - Platform-independent design (can swap with RIO/epoll/io_uring)
//   - Session-independent I/O operations
//   - Used for multi-platform libraries or advanced scenarios
//
// IOCPNetworkEngine:
//   - High-level server engine with Session management
//   - Optimized for Windows server applications
//   - Session lifecycle, event callbacks, thread pools
//   - Direct IOCP usage with Session::IOContext
//
//
// IocpAsyncIOProvider:
//
// IOCPNetworkEngine:
// =============================================================================

#include "Network/Core/AsyncIOProvider.h"

#ifdef _WIN32
#include <atomic>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <string>

namespace Network
{
namespace AsyncIO
{
namespace Windows
{
// =============================================================================
// IOCP-based AsyncIOProvider Implementation
// =============================================================================

class IocpAsyncIOProvider : public AsyncIOProvider
{
  public:
	// Constructor
	IocpAsyncIOProvider();

	// Destructor - releases IOCP resources
	virtual ~IocpAsyncIOProvider();

	// Prevent copy (move-only semantics)
	IocpAsyncIOProvider(const IocpAsyncIOProvider &) = delete;
	IocpAsyncIOProvider &operator=(const IocpAsyncIOProvider &) = delete;

	// =====================================================================
	// Lifecycle Management
	// =====================================================================

	AsyncIOError Initialize(size_t queueDepth, size_t maxConcurrent) override;
	void Shutdown() override;
	bool IsInitialized() const override;

	// =====================================================================
	// Socket Association
	// =====================================================================

	AsyncIOError AssociateSocket(SocketHandle socket,
								RequestContext context) override;

	// =====================================================================
	// Buffer Management
	// =====================================================================

	int64_t RegisterBuffer(const void *ptr, size_t size) override;
	AsyncIOError UnregisterBuffer(int64_t bufferId) override;

	// =====================================================================
	// Async I/O Requests
	// =====================================================================

	AsyncIOError SendAsync(SocketHandle socket, const void *buffer, size_t size,
							   RequestContext context, uint32_t flags = 0) override;

	AsyncIOError RecvAsync(SocketHandle socket, void *buffer, size_t size,
							   RequestContext context, uint32_t flags = 0) override;

	AsyncIOError FlushRequests() override;

	// =====================================================================
	// Completion Processing
	// =====================================================================

	int ProcessCompletions(CompletionEntry *entries, size_t maxEntries,
							   int timeoutMs = 0) override;

	// =====================================================================
	// Information & Statistics
	// =====================================================================

	const ProviderInfo &GetInfo() const override;
	ProviderStats GetStats() const override;
	const char *GetLastError() const override;

  private:
	// =====================================================================
	// Internal Data Structures
	// =====================================================================

	// Pending operation tracking structure
	struct PendingOperation
	{
		OVERLAPPED mOverlapped; // IOCP overlapped structure (must be first member for pointer cast)
		WSABUF mWsaBuffer;      // WSA buffer
		std::unique_ptr<uint8_t[]> mBuffer; // Dynamically allocated buffer
		RequestContext mContext; // User request context
		AsyncIOType mType;       // Operation type
		SocketHandle mSocket = INVALID_SOCKET; // Owning socket - enables O(1) map lookup from OVERLAPPED*
	};

	// =====================================================================
	// Member Variables
	// =====================================================================

	HANDLE mCompletionPort; // IOCP completion port handle
	std::unordered_map<OVERLAPPED *, std::unique_ptr<PendingOperation>>
		mPendingRecvOps; // Pending recv ops
	std::unordered_map<OVERLAPPED *, std::unique_ptr<PendingOperation>>
		mPendingSendOps; // Pending send ops
	mutable std::mutex
		mMutex; // Thread safety mutex
	ProviderInfo mInfo; // Provider info cache
	ProviderStats mStats; // Statistics
	std::string
		mLastError; // Last error message
	size_t mMaxConcurrentOps; // Max concurrent ops
	std::atomic<bool> mInitialized;
	std::atomic<bool> mShuttingDown{false}; // Initialization flag
};

} // namespace Windows
} // namespace AsyncIO
} // namespace Network

#endif // _WIN32
