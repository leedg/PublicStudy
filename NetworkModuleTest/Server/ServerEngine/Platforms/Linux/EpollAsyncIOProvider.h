#pragma once

// epoll-based AsyncIOProvider implementation for Linux

#include "AsyncIOProvider.h"

#ifdef __linux__
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <sys/epoll.h>

namespace Network
{
namespace AsyncIO
{
namespace Linux
{
// =============================================================================
// epoll-based AsyncIOProvider Implementation
// =============================================================================

class EpollAsyncIOProvider : public AsyncIOProvider
{
  public:
	// Constructor
	EpollAsyncIOProvider();

	// Destructor - releases epoll resources
	virtual ~EpollAsyncIOProvider();

	// Prevent copy (move-only semantics)
	EpollAsyncIOProvider(const EpollAsyncIOProvider &) = delete;
	EpollAsyncIOProvider &operator=(const EpollAsyncIOProvider &) = delete;

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
		RequestContext mContext; // User request context
		AsyncIOType mType; // Operation type
		SocketHandle mSocket; // Socket handle
		uint8_t *mBuffer; // Buffer pointer (recv or owned send buffer)
		std::unique_ptr<uint8_t[]> mOwnedBuffer; // Owned buffer for send
		uint32_t mBufferSize; // Buffer size
	};

	// =====================================================================
	// Member Variables
	// =====================================================================

	int mEpollFd; // epoll file descriptor
	std::map<SocketHandle, PendingOperation>
		mPendingRecvOps; // Pending recv operations
	std::map<SocketHandle, PendingOperation>
		mPendingSendOps; // Pending send operations
	mutable std::mutex
		mMutex; // Thread safety mutex
	ProviderInfo mInfo;   // Provider info
	ProviderStats mStats; // Statistics
	std::string
		mLastError; // Last error message
	size_t
		mMaxConcurrentOps; // Max concurrent ops
	bool mInitialized;     // Initialization flag
};

} // namespace Linux
} // namespace AsyncIO
} // namespace Network

#endif // __linux__
