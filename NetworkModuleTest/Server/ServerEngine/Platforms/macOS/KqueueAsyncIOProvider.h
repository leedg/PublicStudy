#pragma once

// kqueue-based AsyncIOProvider implementation for macOS/BSD

#include "AsyncIOProvider.h"

#ifdef __APPLE__
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <sys/event.h>

namespace Network
{
namespace AsyncIO
{
namespace BSD
{
// =============================================================================
// kqueue-based AsyncIOProvider Implementation (macOS/BSD)
// =============================================================================

class KqueueAsyncIOProvider : public AsyncIOProvider
{
  public:
	// Constructor
	KqueueAsyncIOProvider();

	// Destructor - releases kqueue resources
	virtual ~KqueueAsyncIOProvider();

	// Prevent copy (move-only semantics)
	KqueueAsyncIOProvider(const KqueueAsyncIOProvider &) = delete;
	KqueueAsyncIOProvider &operator=(const KqueueAsyncIOProvider &) = delete;

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

	// Pending operation tracking
	struct PendingOperation
	{
		RequestContext mContext; // User request context
		AsyncIOType mType;    // Operation type
		SocketHandle mSocket; // Socket handle
		uint8_t *mBuffer; // Buffer pointer (recv or owned send buffer)
		std::unique_ptr<uint8_t[]> mOwnedBuffer; // Owned buffer for send
		uint32_t mBufferSize; // Buffer size
	};

	// =====================================================================
	// Member Variables
	// =====================================================================

	int mKqueueFd; // kqueue file descriptor
	std::map<SocketHandle, PendingOperation>
		mPendingRecvOps; // Pending recv ops
	std::map<SocketHandle, PendingOperation>
		mPendingSendOps; // Pending send ops
	std::map<SocketHandle, bool>
		mRegisteredSockets; // Registered sockets
	mutable std::mutex
		mMutex; // Thread safety mutex
	ProviderInfo mInfo;   // Provider info
	ProviderStats mStats; // Statistics
	std::string
		mLastError; // Last error message
	size_t
		mMaxConcurrentOps; // Max concurrent ops
	bool mInitialized;     // Initialization flag

	// =====================================================================
	// Helper Methods
	// =====================================================================

	// Register socket with kqueue for read and write events
	bool RegisterSocketEvents(SocketHandle socket);

	// Unregister socket events from kqueue
	bool UnregisterSocketEvents(SocketHandle socket);
};

} // namespace BSD
} // namespace AsyncIO
} // namespace Network

#endif // __APPLE__
