// English: io_uring-based AsyncIOProvider implementation
// ?쒓?: io_uring 湲곕컲 AsyncIOProvider 援ы쁽

#ifdef __linux__

#include "IOUringAsyncIOProvider.h"
#include "PlatformDetect.h"
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
// English: Constructor & Destructor
// ?쒓?: ?앹꽦??諛??뚮㈇??    //
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
// English: Lifecycle Management
// ?쒓?: ?앸챸二쇨린 愿由?
// =============================================================================

AsyncIOError IOUringAsyncIOProvider::Initialize(size_t queueDepth,
												size_t maxConcurrent)
{
	if (mInitialized)
		return AsyncIOError::AlreadyInitialized;

	mMaxConcurrentOps = maxConcurrent;

	// English: Initialize io_uring ring with specified queue depth
	// ?쒓?: 吏?뺣맂 ??源딆씠濡?io_uring 留?珥덇린??
	struct io_uring_params params;
	std::memset(&params, 0, sizeof(params));

	// English: Cap queue depth at 4096 (io_uring limit)
	// ?쒓?: ??源딆씠瑜?4096?쇰줈 ?쒗븳 (io_uring ?쒗븳)
	size_t actualDepth = (queueDepth > 4096) ? 4096 : queueDepth;

	int ret = io_uring_queue_init_params(static_cast<unsigned>(actualDepth),
										 &mRing, &params);
	if (ret < 0)
	{
		mLastError = "io_uring_queue_init_params failed";
		return AsyncIOError::OperationFailed;
	}

	// English: Check feature support
	// ?쒓?: 湲곕뒫 吏???뺤씤
	unsigned int features = mRing.features;
	mSupportsFixedBuffers = (features & IORING_FEAT_FAST_POLL) != 0;
	mSupportsDirectDescriptors = (features & IORING_FEAT_NODROP) != 0;

	// English: Initialize provider info
	// ?쒓?: 怨듦툒???뺣낫 珥덇린??
	mInfo.mPlatformType = PlatformType::IOUring;
	mInfo.mName = "io_uring";
	mInfo.mMaxQueueDepth = actualDepth;
	mInfo.mMaxConcurrentReq = maxConcurrent;
	mInfo.mSupportsBufferReg = mSupportsFixedBuffers;
	mInfo.mSupportsBatching = true;
	mInfo.mSupportsZeroCopy = true;

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

	// English: Exit the ring
	// ?쒓?: 留?醫낅즺
	io_uring_queue_exit(&mRing);
	mInitialized = false;
}

bool IOUringAsyncIOProvider::IsInitialized() const { return mInitialized; }

// =============================================================================
// English: Buffer Management
// ?쒓?: 踰꾪띁 愿由?
// =============================================================================

int64_t IOUringAsyncIOProvider::RegisterBuffer(const void *ptr, size_t size)
{
	if (!mInitialized || !ptr || size == 0)
		return -1;

	std::lock_guard<std::mutex> lock(mMutex);

	// English: Store buffer registration (simple mapping)
	// ?쒓?: 踰꾪띁 ?깅줉 ???(?⑥닚 留ㅽ븨)
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
// English: Async I/O Operations
// ?쒓?: 鍮꾨룞湲?I/O ?묒뾽
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

	std::lock_guard<std::mutex> lock(mMutex);

	// English: Allocate buffer and copy data
	// ?쒓?: 踰꾪띁 ?좊떦 諛??곗씠??蹂듭궗
	auto internalBuffer = std::make_unique<uint8_t[]>(size);
	std::memcpy(internalBuffer.get(), buffer, size);

	// English: Store pending operation
	// ?쒓?: ?湲??묒뾽 ???
	uint64_t opKey = mNextOpKey++;
	PendingOperation pending;
	pending.mContext = context;
	pending.mType = AsyncIOType::Send;
	pending.mSocket = socket;
	pending.mBuffer = std::move(internalBuffer);
	pending.mBufferSize = static_cast<uint32_t>(size);

	mPendingOps[opKey] = std::move(pending);

	// English: Prepare send operation in io_uring SQ
	// ?쒓?: io_uring SQ???≪떊 ?묒뾽 以鍮?
	struct io_uring_sqe *sqe = io_uring_get_sqe(&mRing);
	if (!sqe)
	{
		mLastError = "io_uring SQ full";
		mPendingOps.erase(opKey);
		return AsyncIOError::NoResources;
	}

	io_uring_prep_send(sqe, socket, mPendingOps[opKey].mBuffer.get(), size, 0);
	sqe->user_data = opKey;

	mStats.mTotalRequests++;
	mStats.mPendingRequests++;

	// English: Submit to ring
	// ?쒓?: 留곸뿉 ?쒖텧
	return SubmitRing() ? AsyncIOError::Success : AsyncIOError::OperationFailed;
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

	std::lock_guard<std::mutex> lock(mMutex);

	// English: Store pending operation
	// ?쒓?: ?湲??묒뾽 ???
	uint64_t opKey = mNextOpKey++;
	PendingOperation pending;
	pending.mContext = context;
	pending.mType = AsyncIOType::Recv;
	pending.mSocket = socket;
	pending.mBuffer = nullptr;
	pending.mBufferSize = static_cast<uint32_t>(size);

	mPendingOps[opKey] = std::move(pending);

	// English: Prepare receive operation
	// ?쒓?: ?섏떊 ?묒뾽 以鍮?
	struct io_uring_sqe *sqe = io_uring_get_sqe(&mRing);
	if (!sqe)
	{
		mLastError = "io_uring SQ full";
		mPendingOps.erase(opKey);
		return AsyncIOError::NoResources;
	}

	io_uring_prep_recv(sqe, socket, buffer, size, 0);
	sqe->user_data = opKey;

	mStats.mTotalRequests++;
	mStats.mPendingRequests++;

	return SubmitRing() ? AsyncIOError::Success : AsyncIOError::OperationFailed;
}

AsyncIOError IOUringAsyncIOProvider::FlushRequests()
{
	// English: Submit all SQ entries to kernel
	// ?쒓?: 紐⑤뱺 SQ ??ぉ??而ㅻ꼸???쒖텧
	if (!mInitialized)
		return AsyncIOError::NotInitialized;

	return SubmitRing() ? AsyncIOError::Success : AsyncIOError::OperationFailed;
}

// =============================================================================
// English: Completion Processing
// ?쒓?: ?꾨즺 泥섎━
// =============================================================================

int IOUringAsyncIOProvider::ProcessCompletions(CompletionEntry *entries,
												   size_t maxEntries, int timeoutMs)
{
	if (!mInitialized)
		return static_cast<int>(AsyncIOError::NotInitialized);
	if (!entries || maxEntries == 0)
		return static_cast<int>(AsyncIOError::InvalidParameter);

	std::lock_guard<std::mutex> lock(mMutex);

	// English: Process available completions
	// ?쒓?: ?ъ슜 媛?ν븳 ?꾨즺 泥섎━
	int count = ProcessCompletionQueue(entries, maxEntries);

	// English: If no completions and timeout > 0, wait
	// ?쒓?: ?꾨즺 ?녾퀬 ??꾩븘??> 0?대㈃ ?湲?
	if (count == 0 && timeoutMs != 0)
	{
		struct __kernel_timespec ts;
		ts.tv_sec = (timeoutMs > 0) ? (timeoutMs / 1000) : 0;
		ts.tv_nsec = (timeoutMs > 0) ? ((timeoutMs % 1000) * 1000000) : 0;

		struct io_uring_cqe *cqe;
		int ret = io_uring_wait_cqe_timeout(&mRing, &cqe,
											(timeoutMs > 0) ? &ts : nullptr);
		if (ret == 0)
		{
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
			// English: Fill completion entry
			// ?쒓?: ?꾨즺 ??ぉ 梨꾩슦湲?
			CompletionEntry &entry = entries[processedCount];
			entry.mContext = it->second.mContext;
			entry.mType = it->second.mType;
			entry.mResult = static_cast<int32_t>(res);
			entry.mOsError = (res < 0) ? static_cast<OSError>(-res) : 0;
			entry.mCompletionTime = 0;

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
// English: Helper Methods
// ?쒓?: ?ы띁 硫붿꽌??
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
// English: Information & Statistics
// ?쒓?: ?뺣낫 諛??듦퀎
// =============================================================================

const ProviderInfo &IOUringAsyncIOProvider::GetInfo() const { return mInfo; }

ProviderStats IOUringAsyncIOProvider::GetStats() const { return mStats; }

const char *IOUringAsyncIOProvider::GetLastError() const
{
	return mLastError.c_str();
}

// =============================================================================
// English: Factory Function
// ?쒓?: ?⑺넗由??⑥닔
// =============================================================================

std::unique_ptr<AsyncIOProvider> CreateIOUringProvider()
{
	return std::unique_ptr<AsyncIOProvider>(new IOUringAsyncIOProvider());
}

} // namespace Linux
} // namespace AsyncIO
} // namespace Network

#endif // __linux__
