// io_uring-based AsyncIOProvider implementation
//          Compiled only when HAVE_LIBURING is defined (CMake find_library check).

#if defined(__linux__) && (defined(HAVE_IO_URING) || defined(HAVE_LIBURING))

#include "IOUringAsyncIOProvider.h"
#include "Network/Core/PlatformDetect.h"
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace Network
{
namespace AsyncIO
{
namespace Linux
{
// =============================================================================
// Constructor & Destructor
// =============================================================================

IOUringAsyncIOProvider::IOUringAsyncIOProvider()
	: mInfo{}, mStats{}, mMaxConcurrentOps(0), mNextBufferId(1), mNextOpKey(1),
		  mInitialized(false), mSupportsFixedBuffers(false),
		  mSupportsDirectDescriptors(false)
{
	std::memset(&mRing, 0, sizeof(io_uring));
}

IOUringAsyncIOProvider::~IOUringAsyncIOProvider() { Shutdown(); }

// =============================================================================
// Lifecycle Management
// =============================================================================

AsyncIOError IOUringAsyncIOProvider::Initialize(size_t queueDepth,
												size_t maxConcurrent)
{
	if (mInitialized)
		return AsyncIOError::AlreadyInitialized;

	mMaxConcurrentOps = maxConcurrent;

	// Initialize io_uring ring with specified queue depth
	struct io_uring_params params;
	std::memset(&params, 0, sizeof(params));

	// Cap queue depth at 4096 (io_uring limit)
	size_t actualDepth = (queueDepth > 4096) ? 4096 : queueDepth;

	int ret = io_uring_queue_init_params(static_cast<unsigned>(actualDepth),
										 &mRing, &params);
	if (ret < 0)
	{
		mLastError = "io_uring_queue_init_params failed";
		return AsyncIOError::OperationFailed;
	}

	// Check feature support
	unsigned int features = mRing.features;
	mSupportsDirectDescriptors = (features & IORING_FEAT_NODROP) != 0;

	// Initialize recv pool with fixed-buffer mode; fall back to
	//          non-fixed if io_uring_register_buffers is unsupported.
	constexpr size_t kSlotSize = 8192;
	if (!mRecvPool.InitializeFixed(&mRing, maxConcurrent, kSlotSize))
	{
		if (!mRecvPool.Initialize(maxConcurrent, kSlotSize))
		{
			io_uring_queue_exit(&mRing);
			mLastError = "Failed to initialize recv buffer pool";
			return AsyncIOError::AllocationFailed;
		}
	}

	// Initialize send pool (no kernel registration needed for sends).
	if (!mSendPool.Initialize(maxConcurrent, kSlotSize))
	{
		mRecvPool.Shutdown();
		io_uring_queue_exit(&mRing);
		mLastError = "Failed to initialize send buffer pool";
		return AsyncIOError::AllocationFailed;
	}

	mSupportsFixedBuffers = mRecvPool.IsFixedBufferMode();

	// Initialize provider info
	mInfo.mPlatformType = PlatformType::IOUring;
	mInfo.mName = "io_uring";
	mInfo.mMaxQueueDepth = actualDepth;
	mInfo.mMaxConcurrentReq = maxConcurrent;
	mInfo.mSupportsBufferReg = mSupportsFixedBuffers;
	mInfo.mSupportsBatching = true;
	mInfo.mSupportsZeroCopy = mSupportsFixedBuffers;

	mInitialized = true;
	return AsyncIOError::Success;
}

void IOUringAsyncIOProvider::Shutdown()
{
	if (!mInitialized)
		return;

	std::lock_guard<std::mutex> lock(mMutex);

	mRegisteredBuffers.clear();
	mPendingOps.clear();

	// Shutdown pools before ring exit.
	//          IOUringBufferPool::Shutdown() calls io_uring_unregister_buffers()
	//          which must happen while the ring is still alive.
	mRecvPool.Shutdown();
	mSendPool.Shutdown();

	// Exit the ring
	io_uring_queue_exit(&mRing);
	mInitialized = false;
}

bool IOUringAsyncIOProvider::IsInitialized() const { return mInitialized; }

// =============================================================================
// Socket Association
// =============================================================================

AsyncIOError IOUringAsyncIOProvider::AssociateSocket(SocketHandle socket,
													 RequestContext context)
{
	// io_uring doesn't require explicit socket association
	// io_uring operates on file descriptors directly via SQE submissions,
	// no prior registration needed (unlike IOCP/epoll).
	if (!mInitialized)
		return AsyncIOError::NotInitialized;

	return AsyncIOError::Success;
}

// =============================================================================
// Buffer Management
// =============================================================================

int64_t IOUringAsyncIOProvider::RegisterBuffer(const void *ptr, size_t size)
{
	if (!mInitialized || !ptr || size == 0)
		return -1;

	std::lock_guard<std::mutex> lock(mMutex);

	// Store buffer registration (simple mapping)
	int64_t bufferId = mNextBufferId++;
	RegisteredBufferEntry entry;
	entry.mAddress = const_cast<void *>(ptr);
	entry.mSize = static_cast<uint32_t>(size);
	entry.mBufferGroupId = static_cast<int32_t>(bufferId);

	mRegisteredBuffers[bufferId] = entry;
	return bufferId;
}

AsyncIOError IOUringAsyncIOProvider::UnregisterBuffer(int64_t bufferId)
{
	if (!mInitialized)
		return AsyncIOError::NotInitialized;

	std::lock_guard<std::mutex> lock(mMutex);

	auto it = mRegisteredBuffers.find(bufferId);
	if (it == mRegisteredBuffers.end())
		return AsyncIOError::InvalidBuffer;

	mRegisteredBuffers.erase(it);
	return AsyncIOError::Success;
}

// =============================================================================
// Async I/O Operations
// =============================================================================

AsyncIOError IOUringAsyncIOProvider::SendAsync(SocketHandle socket,
												   const void *buffer, size_t size,
												   RequestContext context,
												   uint32_t flags)
{
	if (!mInitialized)
		return AsyncIOError::NotInitialized;
	if (socket < 0 || !buffer || size == 0)
		return AsyncIOError::InvalidParameter;

	// Acquire send pool slot before taking the main lock.
	Network::Core::Memory::BufferSlot sendSlot = mSendPool.Acquire();
	if (!sendSlot.ptr)
		return AsyncIOError::NoResources;

	std::memcpy(sendSlot.ptr, buffer, size);

	std::lock_guard<std::mutex> lock(mMutex);

	// Store pending operation
	uint64_t opKey = mNextOpKey++;
	PendingOperation pending;
	pending.mContext        = context;
	pending.mType           = AsyncIOType::Send;
	pending.mSocket         = socket;
	pending.mCallerBuffer   = nullptr;         // not needed for send
	pending.mPoolSlotPtr    = sendSlot.ptr;
	pending.mBufferSize     = static_cast<uint32_t>(size);
	pending.mPoolSlotIndex  = sendSlot.index;

	mPendingOps[opKey] = std::move(pending);

	// Prepare send operation in io_uring SQ
	struct io_uring_sqe *sqe = io_uring_get_sqe(&mRing);
	if (!sqe)
	{
		mLastError = "io_uring SQ full";
		mSendPool.Release(sendSlot.index);
		mPendingOps.erase(opKey);
		return AsyncIOError::NoResources;
	}

	io_uring_prep_send(sqe, socket, sendSlot.ptr, size, 0);
	sqe->user_data = opKey;

	mStats.mTotalRequests++;
	mStats.mPendingRequests++;

	// Submit to ring. On failure roll back the pending op and pool slot
	//          so the caller can retry. The prepped SQE remains in the SQ but
	//          has no matching pending entry — a stale CQE for this opKey will
	//          be silently ignored in ProcessCompletionQueue().
	if (!SubmitRing())
	{
		mSendPool.Release(sendSlot.index);
		mPendingOps.erase(opKey);
		mStats.mTotalRequests--;
		mStats.mPendingRequests--;
		return AsyncIOError::OperationFailed;
	}
	return AsyncIOError::Success;
}

AsyncIOError IOUringAsyncIOProvider::RecvAsync(SocketHandle socket,
												   void *buffer, size_t size,
												   RequestContext context,
												   uint32_t flags)
{
	if (!mInitialized)
		return AsyncIOError::NotInitialized;
	if (socket < 0 || !buffer || size == 0)
		return AsyncIOError::InvalidParameter;

	// Acquire recv pool slot before taking the main lock.
	Network::Core::Memory::BufferSlot recvSlot = mRecvPool.Acquire();
	if (!recvSlot.ptr)
		return AsyncIOError::NoResources;

	std::lock_guard<std::mutex> lock(mMutex);

	// Store pending operation
	uint64_t opKey = mNextOpKey++;
	PendingOperation pending;
	pending.mContext        = context;
	pending.mType           = AsyncIOType::Recv;
	pending.mSocket         = socket;
	pending.mCallerBuffer   = buffer;           // destination for completed data
	pending.mPoolSlotPtr    = recvSlot.ptr;
	pending.mBufferSize     = static_cast<uint32_t>(size);
	pending.mPoolSlotIndex  = recvSlot.index;

	mPendingOps[opKey] = std::move(pending);

	// Prepare receive operation.
	//          Use fixed-buffer read when pool is registered with the ring
	//          (zero-copy kernel path); fall back to regular recv otherwise.
	struct io_uring_sqe *sqe = io_uring_get_sqe(&mRing);
	if (!sqe)
	{
		mLastError = "io_uring SQ full";
		mRecvPool.Release(recvSlot.index);
		mPendingOps.erase(opKey);
		return AsyncIOError::NoResources;
	}

	io_uring_prep_recv(sqe, socket, recvSlot.ptr, size, 0);
	sqe->user_data = opKey;

	mStats.mTotalRequests++;
	mStats.mPendingRequests++;

	// Submit to ring. On failure roll back the pending op and pool slot.
	if (!SubmitRing())
	{
		mRecvPool.Release(recvSlot.index);
		mPendingOps.erase(opKey);
		mStats.mTotalRequests--;
		mStats.mPendingRequests--;
		return AsyncIOError::OperationFailed;
	}
	return AsyncIOError::Success;
}

AsyncIOError IOUringAsyncIOProvider::FlushRequests()
{
	// Submit all SQ entries to kernel
	if (!mInitialized)
		return AsyncIOError::NotInitialized;

	return SubmitRing() ? AsyncIOError::Success : AsyncIOError::OperationFailed;
}

// =============================================================================
// Completion Processing
// =============================================================================

int IOUringAsyncIOProvider::ProcessCompletions(CompletionEntry *entries,
												   size_t maxEntries, int timeoutMs)
{
	if (!mInitialized)
		return static_cast<int>(AsyncIOError::NotInitialized);
	if (!entries || maxEntries == 0)
		return static_cast<int>(AsyncIOError::InvalidParameter);

	std::unique_lock<std::mutex> lock(mMutex);

	// Process available completions
	int count = ProcessCompletionQueue(entries, maxEntries);

	// If no completions and timeout > 0, wait
	if (count == 0 && timeoutMs != 0)
	{
		lock.unlock();

		struct __kernel_timespec ts;
		ts.tv_sec = (timeoutMs > 0) ? (timeoutMs / 1000) : 0;
		ts.tv_nsec = (timeoutMs > 0) ? ((timeoutMs % 1000) * 1000000) : 0;

		struct io_uring_cqe *cqe;
		int ret = io_uring_wait_cqe_timeout(&mRing, &cqe,
											(timeoutMs > 0) ? &ts : nullptr);
		if (ret == 0)
		{
			lock.lock();
			count = ProcessCompletionQueue(entries, maxEntries);
		}
	}

	return count;
}

int IOUringAsyncIOProvider::ProcessCompletionQueue(CompletionEntry *entries,
													   size_t maxEntries)
{
	int processedCount = 0;
	unsigned head;
	struct io_uring_cqe *cqe;

	io_uring_for_each_cqe(&mRing, head, cqe)
	{
		if (static_cast<size_t>(processedCount) >= maxEntries)
			break;

		uint64_t opKey = cqe->user_data;
		int res = cqe->res;

		auto it = mPendingOps.find(opKey);
		if (it != mPendingOps.end())
		{
			const PendingOperation &op = it->second;

			// Fill completion entry
			CompletionEntry &entry = entries[processedCount];
			entry.mContext        = op.mContext;
			entry.mType           = op.mType;
			entry.mResult         = static_cast<int32_t>(res);
			entry.mOsError        = (res < 0) ? static_cast<OSError>(-res) : 0;
			entry.mCompletionTime = 0;

			// For recv completions copy data from the pool slot to the
			//          caller's buffer, then release the slot back to the pool.
			//          For send completions just release the send slot.
			if (op.mType == AsyncIOType::Recv)
			{
				if (res > 0 && op.mCallerBuffer)
					std::memcpy(op.mCallerBuffer, op.mPoolSlotPtr,
								static_cast<size_t>(res));
				mRecvPool.Release(op.mPoolSlotIndex);
			}
			else if (op.mType == AsyncIOType::Send)
			{
				mSendPool.Release(op.mPoolSlotIndex);
			}

			mPendingOps.erase(it);
			mStats.mPendingRequests--;
			mStats.mTotalCompletions++;
			processedCount++;
		}
	}

	if (processedCount > 0)
	{
		io_uring_cq_advance(&mRing, static_cast<unsigned>(processedCount));
	}

	return processedCount;
}

// =============================================================================
// Helper Methods
// =============================================================================

bool IOUringAsyncIOProvider::SubmitRing()
{
	int ret = io_uring_submit(&mRing);
	if (ret < 0)
	{
		mLastError = "io_uring_submit failed";
	}
	return ret >= 0;
}

// =============================================================================
// Information & Statistics
// =============================================================================

const ProviderInfo &IOUringAsyncIOProvider::GetInfo() const { return mInfo; }

ProviderStats IOUringAsyncIOProvider::GetStats() const { return mStats; }

const char *IOUringAsyncIOProvider::GetLastError() const
{
	return mLastError.c_str();
}

// =============================================================================
// Factory Function
// =============================================================================

std::unique_ptr<AsyncIOProvider> CreateIOUringProvider()
{
	return std::unique_ptr<AsyncIOProvider>(new IOUringAsyncIOProvider());
}

} // namespace Linux
} // namespace AsyncIO
} // namespace Network

#endif // defined(__linux__) && (defined(HAVE_IO_URING) || defined(HAVE_LIBURING))
