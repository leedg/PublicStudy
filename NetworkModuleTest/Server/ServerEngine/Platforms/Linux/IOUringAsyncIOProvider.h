#pragma once

// io_uring-based AsyncIOProvider implementation for Linux kernel 5.1+
//          Requires liburing-dev (apt install liburing-dev / dnf install liburing-devel).
//          Enabled only when HAVE_LIBURING is defined by the build system (CMake find_library check).
//          If liburing is unavailable the factory falls back to epoll automatically.

#include "Network/Core/AsyncIOProvider.h"

#if defined(__linux__) && (defined(HAVE_IO_URING) || defined(HAVE_LIBURING))
#include "../../Core/Memory/IOUringBufferPool.h"
#include "../../Core/Memory/StandardBufferPool.h"
#include <liburing.h>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace Network
{
namespace AsyncIO
{
namespace Linux
{
// =============================================================================
// io_uring-based AsyncIOProvider Implementation (Linux kernel 5.1+)
// =============================================================================

class IOUringAsyncIOProvider : public AsyncIOProvider
{
  public:
	// Constructor
	IOUringAsyncIOProvider();

	// Destructor - releases io_uring resources
	virtual ~IOUringAsyncIOProvider();

	// Prevent copy (move-only semantics)
	IOUringAsyncIOProvider(const IOUringAsyncIOProvider &) = delete;
	IOUringAsyncIOProvider &operator=(const IOUringAsyncIOProvider &) = delete;

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
		RequestContext mContext;      // User request context
		AsyncIOType    mType;         // Operation type
		SocketHandle   mSocket;       // Socket handle
		void*          mCallerBuffer; // Recv destination (nullptr for send)
		void*          mPoolSlotPtr;  // Pool slot pointer (recv fixed buf or send buf)
		uint32_t       mBufferSize;   // Buffer size
		size_t         mPoolSlotIndex;// Pool slot index for Release()
	};

	// Registered buffer info
	struct RegisteredBufferEntry
	{
		void *mAddress;         // Buffer address
		uint32_t mSize;         // Buffer size
		int32_t mBufferGroupId; // Buffer group ID
	};

	// =====================================================================
	// Member Variables
	// =====================================================================

	io_uring mRing; // io_uring ring
	std::map<uint64_t, PendingOperation>
		mPendingOps; // Pending ops by user_data
	std::map<int64_t, RegisteredBufferEntry>
		mRegisteredBuffers; // Registered buffers
	mutable std::mutex
		mMutex; // Thread safety mutex
	ProviderInfo mInfo;   // Provider info
	ProviderStats mStats; // Statistics
	std::string
		mLastError; // Last error message
	size_t
		mMaxConcurrentOps; // Max concurrent ops
	int64_t mNextBufferId; // Next buffer ID
	uint64_t mNextOpKey;   // Next operation key
	bool mInitialized;     // Initialization flag
	bool mSupportsFixedBuffers; // Fixed buffer support
	bool mSupportsDirectDescriptors; // Direct descriptor support /

	// Pre-allocated buffer pools.
	//          mRecvPool: fixed-buffer recv (io_uring_register_buffers); falls
	//          back to non-fixed if kernel doesn't support registration.
	//          mSendPool: standard aligned pool for staging send data.
	::Network::Core::Memory::IOUringBufferPool  mRecvPool;
	::Network::Core::Memory::StandardBufferPool mSendPool;

	// =====================================================================
	// Helper Methods
	// =====================================================================

	// Submit pending operations to the ring
	bool SubmitRing();

	// Process completion queue entries
	int ProcessCompletionQueue(CompletionEntry *entries, size_t maxEntries);
};

} // namespace Linux
} // namespace AsyncIO
} // namespace Network

#endif // defined(__linux__) && (defined(HAVE_IO_URING) || defined(HAVE_LIBURING))
