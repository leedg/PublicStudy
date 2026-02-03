// English: epoll-based AsyncIOProvider implementation
// ?쒓?: epoll 湲곕컲 AsyncIOProvider 援ы쁽

#ifdef __linux__

#include "EpollAsyncIOProvider.h"
#include "PlatformDetect.h"
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
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

EpollAsyncIOProvider::EpollAsyncIOProvider()
	: mEpollFd(-1), mInfo{}, mStats{}, mMaxConcurrentOps(0), mInitialized(false)
{
}

EpollAsyncIOProvider::~EpollAsyncIOProvider()
{
	// English: Ensure resources are released
	// ?쒓?: 由ъ냼???댁젣 蹂댁옣
	Shutdown();
}

// =============================================================================
// English: Lifecycle Management
// ?쒓?: ?앸챸二쇨린 愿由?
// =============================================================================

AsyncIOError EpollAsyncIOProvider::Initialize(size_t queueDepth,
												  size_t maxConcurrent)
{
	if (mInitialized)
		return AsyncIOError::AlreadyInitialized;

	// English: Create epoll file descriptor with close-on-exec
	// ?쒓?: close-on-exec濡?epoll ?뚯씪 ?붿뒪?щ┰???앹꽦
	mEpollFd = epoll_create1(EPOLL_CLOEXEC);
	if (mEpollFd < 0)
	{
		mLastError = "epoll_create1 failed";
		return AsyncIOError::OperationFailed;
	}

	mMaxConcurrentOps = maxConcurrent;

	// English: Initialize provider info
	// ?쒓?: 怨듦툒???뺣낫 珥덇린??
	mInfo.mPlatformType = PlatformType::Epoll;
	mInfo.mName = "epoll";
	mInfo.mMaxQueueDepth = queueDepth;
	mInfo.mMaxConcurrentReq = maxConcurrent;
	mInfo.mSupportsBufferReg = false;
	mInfo.mSupportsBatching = false;
	mInfo.mSupportsZeroCopy = false;

	mInitialized = true;
	return AsyncIOError::Success;
}

void EpollAsyncIOProvider::Shutdown()
{
	if (!mInitialized)
		return;

	std::lock_guard<std::mutex> lock(mMutex);

	// English: Close epoll file descriptor
	// ?쒓?: epoll ?뚯씪 ?붿뒪?щ┰???リ린
	if (mEpollFd >= 0)
	{
		close(mEpollFd);
		mEpollFd = -1;
	}

	mPendingOps.clear();
	mInitialized = false;
}

bool EpollAsyncIOProvider::IsInitialized() const { return mInitialized; }

// =============================================================================
// English: Buffer Management
// ?쒓?: 踰꾪띁 愿由?
// =============================================================================

int64_t EpollAsyncIOProvider::RegisterBuffer(const void *ptr, size_t size)
{
	// English: epoll doesn't support pre-registered buffers (no-op)
	// ?쒓?: epoll? ?ъ쟾 ?깅줉 踰꾪띁瑜?吏?먰븯吏 ?딆쓬
	// (no-op)
	return -1;
}

AsyncIOError EpollAsyncIOProvider::UnregisterBuffer(int64_t bufferId)
{
	// English: Not supported on epoll
	// ?쒓?: epoll?먯꽌 吏?먰븯吏 ?딆쓬
	return AsyncIOError::PlatformNotSupported;
}

// =============================================================================
// English: Async I/O Operations
// ?쒓?: 鍮꾨룞湲?I/O ?묒뾽
// =============================================================================

AsyncIOError EpollAsyncIOProvider::SendAsync(SocketHandle socket,
											 const void *buffer, size_t size,
											 RequestContext context,
											 uint32_t flags)
{
	if (!mInitialized)
		return AsyncIOError::NotInitialized;
	if (socket < 0 || !buffer || size == 0)
		return AsyncIOError::InvalidParameter;

	std::lock_guard<std::mutex> lock(mMutex);

	// English: Store pending operation with buffer copy
	// ?쒓?: 踰꾪띁 蹂듭궗? ?④퍡 ?湲??묒뾽 ???
	PendingOperation pending;
	pending.mContext = context;
	pending.mType = AsyncIOType::Send;
	pending.mBuffer = std::make_unique<uint8_t[]>(size);
	std::memcpy(pending.mBuffer.get(), buffer, size);
	pending.mBufferSize = static_cast<uint32_t>(size);

	mPendingOps[socket] = std::move(pending);
	mStats.mTotalRequests++;
	mStats.mPendingRequests++;

	return AsyncIOError::Success;
}

AsyncIOError EpollAsyncIOProvider::RecvAsync(SocketHandle socket, void *buffer,
											 size_t size,
											 RequestContext context,
											 uint32_t flags)
{
	if (!mInitialized)
		return AsyncIOError::NotInitialized;
	if (socket < 0 || !buffer || size == 0)
		return AsyncIOError::InvalidParameter;

	std::lock_guard<std::mutex> lock(mMutex);

	// English: Store pending operation (caller manages buffer)
	// ?쒓?: ?湲??묒뾽 ???(?몄텧?먭? 踰꾪띁 愿由?
	PendingOperation pending;
	pending.mContext = context;
	pending.mType = AsyncIOType::Recv;
	pending.mBuffer.reset();
	pending.mBufferSize = static_cast<uint32_t>(size);

	mPendingOps[socket] = std::move(pending);
	mStats.mTotalRequests++;
	mStats.mPendingRequests++;

	return AsyncIOError::Success;
}

AsyncIOError EpollAsyncIOProvider::FlushRequests()
{
	// English: epoll doesn't support batch processing (no-op)
	// ?쒓?: epoll? 諛곗튂 泥섎━瑜?吏?먰븯吏 ?딆쓬 (no-op)
	if (!mInitialized)
		return AsyncIOError::NotInitialized;

	return AsyncIOError::Success;
}

// =============================================================================
// English: Completion Processing
// ?쒓?: ?꾨즺 泥섎━
// =============================================================================

int EpollAsyncIOProvider::ProcessCompletions(CompletionEntry *entries,
											 size_t maxEntries, int timeoutMs)
{
	if (!mInitialized)
		return static_cast<int>(AsyncIOError::NotInitialized);
	if (!entries || maxEntries == 0 || mEpollFd < 0)
		return static_cast<int>(AsyncIOError::InvalidParameter);

	// English: Poll for events
	// ?쒓?: ?대깽???대쭅
	std::unique_ptr<struct epoll_event[]> events(
		new struct epoll_event[maxEntries]);
	int numEvents = epoll_wait(mEpollFd, events.get(),
								   static_cast<int>(maxEntries), timeoutMs);

	if (numEvents < 0)
	{
		mLastError = "epoll_wait failed";
		return static_cast<int>(AsyncIOError::OperationFailed);
	}

	if (numEvents == 0)
		return 0;

	int processedCount = 0;

	for (int i = 0;
		 i < numEvents && processedCount < static_cast<int>(maxEntries); ++i)
	{
		SocketHandle socket = events[i].data.fd;

		std::lock_guard<std::mutex> lock(mMutex);
		auto it = mPendingOps.find(socket);
		if (it != mPendingOps.end())
		{
			// English: Fill completion entry
			// ?쒓?: ?꾨즺 ??ぉ 梨꾩슦湲?
			CompletionEntry &entry = entries[processedCount];
			entry.mContext = it->second.mContext;
			entry.mType = it->second.mType;
			entry.mResult = static_cast<int32_t>(it->second.mBufferSize);
			entry.mOsError = 0;
			entry.mCompletionTime = 0;

			mPendingOps.erase(it);
			mStats.mPendingRequests--;
			mStats.mTotalCompletions++;
			processedCount++;
		}
	}

	return processedCount;
}

// =============================================================================
// English: Information & Statistics
// ?쒓?: ?뺣낫 諛??듦퀎
// =============================================================================

const ProviderInfo &EpollAsyncIOProvider::GetInfo() const { return mInfo; }

ProviderStats EpollAsyncIOProvider::GetStats() const { return mStats; }

const char *EpollAsyncIOProvider::GetLastError() const
{
	return mLastError.c_str();
}

// =============================================================================
// English: Factory Function
// ?쒓?: ?⑺넗由??⑥닔
// =============================================================================

std::unique_ptr<AsyncIOProvider> CreateEpollProvider()
{
	return std::unique_ptr<AsyncIOProvider>(new EpollAsyncIOProvider());
}

} // namespace Linux
} // namespace AsyncIO
} // namespace Network

#endif // __linux__
