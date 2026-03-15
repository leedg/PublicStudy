// kqueue-based AsyncIOProvider implementation for macOS/BSD

#ifdef __APPLE__

#include "KqueueAsyncIOProvider.h"
#include "PlatformDetect.h"
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>

namespace Network
{
namespace AsyncIO
{
namespace BSD
{
// =============================================================================
// Constructor & Destructor
// =============================================================================

KqueueAsyncIOProvider::KqueueAsyncIOProvider()
	: mKqueueFd(-1), mInfo{}, mStats{}, mMaxConcurrentOps(0),
		  mInitialized(false)
{
}

KqueueAsyncIOProvider::~KqueueAsyncIOProvider() { Shutdown(); }

// =============================================================================
// Lifecycle Management
// =============================================================================

AsyncIOError KqueueAsyncIOProvider::Initialize(size_t queueDepth,
												   size_t maxConcurrent)
{
	if (mInitialized)
		return AsyncIOError::AlreadyInitialized;

	// Create kqueue file descriptor
	mKqueueFd = kqueue();
	if (mKqueueFd < 0)
	{
		mLastError = "kqueue() failed";
		return AsyncIOError::OperationFailed;
	}

	mMaxConcurrentOps = maxConcurrent;

	// Initialize provider info
	mInfo.mPlatformType = PlatformType::Kqueue;
	mInfo.mName = "kqueue";
	mInfo.mMaxQueueDepth = queueDepth;
	mInfo.mMaxConcurrentReq = maxConcurrent;
	mInfo.mSupportsBufferReg = false;
	mInfo.mSupportsBatching = false;
	mInfo.mSupportsZeroCopy = false;

	mInitialized = true;
	return AsyncIOError::Success;
}

void KqueueAsyncIOProvider::Shutdown()
{
	if (!mInitialized)
		return;

	std::lock_guard<std::mutex> lock(mMutex);

	// Close kqueue file descriptor
	if (mKqueueFd >= 0)
	{
		close(mKqueueFd);
		mKqueueFd = -1;
	}

	mPendingRecvOps.clear();
	mPendingSendOps.clear();
	mRegisteredSockets.clear();
	mInitialized = false;
}

bool KqueueAsyncIOProvider::IsInitialized() const { return mInitialized; }

// =============================================================================
// Socket Association
// =============================================================================

AsyncIOError KqueueAsyncIOProvider::AssociateSocket(SocketHandle socket,
													RequestContext context)
{
	if (!mInitialized)
		return AsyncIOError::NotInitialized;

	// Register socket with kqueue for read/write events
	if (!RegisterSocketEvents(socket))
	{
		mLastError = "Failed to register socket events with kqueue";
		return AsyncIOError::OperationFailed;
	}

	std::lock_guard<std::mutex> lock(mMutex);
	mRegisteredSockets[socket] = true;

	return AsyncIOError::Success;
}

// =============================================================================
// Buffer Management
// =============================================================================

int64_t KqueueAsyncIOProvider::RegisterBuffer(const void *ptr, size_t size)
{
	// kqueue doesn't support pre-registered buffers (no-op)
	return -1;
}

AsyncIOError KqueueAsyncIOProvider::UnregisterBuffer(int64_t bufferId)
{
	return AsyncIOError::PlatformNotSupported;
}

// =============================================================================
// Async I/O Operations
// =============================================================================

AsyncIOError KqueueAsyncIOProvider::SendAsync(SocketHandle socket,
												  const void *buffer, size_t size,
												  RequestContext context,
												  uint32_t flags)
{
	if (!mInitialized)
		return AsyncIOError::NotInitialized;
	if (socket < 0 || !buffer || size == 0)
		return AsyncIOError::InvalidParameter;

	std::lock_guard<std::mutex> lock(mMutex);

	// Store pending operation with buffer copy
	PendingOperation pending;
	pending.mContext = context;
	pending.mType = AsyncIOType::Send;
	pending.mSocket = socket;
	pending.mOwnedBuffer = std::make_unique<uint8_t[]>(size);
	std::memcpy(pending.mOwnedBuffer.get(), buffer, size);
	pending.mBuffer = pending.mOwnedBuffer.get();
	pending.mBufferSize = static_cast<uint32_t>(size);

	mPendingSendOps[socket] = std::move(pending);
	mStats.mTotalRequests++;
	mStats.mPendingRequests++;

	// Dynamically add EVFILT_WRITE so we get notified when socket is writable
	struct kevent ev;
	EV_SET(&ev, socket, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, nullptr);
	kevent(mKqueueFd, &ev, 1, nullptr, 0, nullptr);

	return AsyncIOError::Success;
}

AsyncIOError KqueueAsyncIOProvider::RecvAsync(SocketHandle socket, void *buffer,
												  size_t size,
												  RequestContext context,
												  uint32_t flags)
{
	if (!mInitialized)
		return AsyncIOError::NotInitialized;
	if (socket < 0 || !buffer || size == 0)
		return AsyncIOError::InvalidParameter;

	std::lock_guard<std::mutex> lock(mMutex);

	PendingOperation pending;
	pending.mContext = context;
	pending.mType = AsyncIOType::Recv;
	pending.mSocket = socket;
	pending.mOwnedBuffer.reset();
	pending.mBuffer = static_cast<uint8_t*>(buffer);
	pending.mBufferSize = static_cast<uint32_t>(size);

	mPendingRecvOps[socket] = std::move(pending);
	mStats.mTotalRequests++;
	mStats.mPendingRequests++;

	return AsyncIOError::Success;
}

AsyncIOError KqueueAsyncIOProvider::FlushRequests()
{
	// kqueue doesn't support batch processing (no-op)
	if (!mInitialized)
		return AsyncIOError::NotInitialized;

	return AsyncIOError::Success;
}

// =============================================================================
// Completion Processing
// =============================================================================

int KqueueAsyncIOProvider::ProcessCompletions(CompletionEntry *entries,
												  size_t maxEntries, int timeoutMs)
{
	if (!mInitialized)
		return static_cast<int>(AsyncIOError::NotInitialized);
	if (!entries || maxEntries == 0 || mKqueueFd < 0)
		return static_cast<int>(AsyncIOError::InvalidParameter);

	// Prepare timeout structure
	struct timespec ts;
	struct timespec *pts = nullptr;

	if (timeoutMs >= 0)
	{
		ts.tv_sec = timeoutMs / 1000;
		ts.tv_nsec = (timeoutMs % 1000) * 1000000;
		pts = &ts;
	}

	// Poll for events
	std::unique_ptr<struct kevent[]> events(new struct kevent[maxEntries]);
	int numEvents = kevent(mKqueueFd, nullptr, 0, events.get(),
							   static_cast<int>(maxEntries), pts);

	if (numEvents <= 0)
		return 0;

	int processedCount = 0;

	for (int i = 0;
		 i < numEvents && processedCount < static_cast<int>(maxEntries); ++i)
	{
		struct kevent &event = events[i];
		SocketHandle socket = static_cast<SocketHandle>(event.ident);

		// Handle errors/EOF
		if (event.flags & EV_ERROR)
		{
			std::lock_guard<std::mutex> lock(mMutex);
			RequestContext ctx = 0;
			auto rit = mPendingRecvOps.find(socket);
			if (rit != mPendingRecvOps.end())
			{
				ctx = rit->second.mContext;
				mPendingRecvOps.erase(rit);
				mStats.mPendingRequests--;
			}
			auto sit = mPendingSendOps.find(socket);
			if (sit != mPendingSendOps.end())
			{
				if (ctx == 0) ctx = sit->second.mContext;
				mPendingSendOps.erase(sit);
				mStats.mPendingRequests--;
			}
			if (ctx != 0)
			{
				CompletionEntry &entry = entries[processedCount];
				entry.mContext = ctx;
				entry.mType = AsyncIOType::Recv;
				entry.mResult = -1;
				entry.mOsError = static_cast<OSError>(event.data);
				entry.mCompletionTime = 0;
				mStats.mTotalCompletions++;
				processedCount++;
			}
			continue;
		}

		if (event.filter == EVFILT_READ)
		{
			PendingOperation pending;
			bool found = false;
			{
				std::lock_guard<std::mutex> lock(mMutex);
				auto it = mPendingRecvOps.find(socket);
				if (it != mPendingRecvOps.end())
				{
					pending = std::move(it->second);
					mPendingRecvOps.erase(it);
					mStats.mPendingRequests--;
					found = true;
				}
			}

			if (!found) continue;

			int32_t result = 0;
			OSError osError = 0;
			ssize_t received = ::recv(socket, pending.mBuffer, pending.mBufferSize, 0);
			if (received >= 0)
			{
				result = static_cast<int32_t>(received);
			}
			else
			{
				if (errno == EAGAIN || errno == EWOULDBLOCK)
				{
					std::lock_guard<std::mutex> lock(mMutex);
					mPendingRecvOps[socket] = std::move(pending);
					mStats.mPendingRequests++;
					continue;
				}
				osError = errno;
				result = -1;
			}

			CompletionEntry &entry = entries[processedCount];
			entry.mContext = pending.mContext;
			entry.mType = AsyncIOType::Recv;
			entry.mResult = result;
			entry.mOsError = osError;
			entry.mCompletionTime = 0;
			mStats.mTotalCompletions++;
			processedCount++;
		}
		else if (event.filter == EVFILT_WRITE)
		{
			PendingOperation pending;
			bool found = false;
			{
				std::lock_guard<std::mutex> lock(mMutex);
				auto it = mPendingSendOps.find(socket);
				if (it != mPendingSendOps.end())
				{
					pending = std::move(it->second);
					mPendingSendOps.erase(it);
					mStats.mPendingRequests--;
					found = true;
				}
			}

			// Remove EVFILT_WRITE after consuming send op (or if none found)
			struct kevent delev;
			EV_SET(&delev, socket, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
			kevent(mKqueueFd, &delev, 1, nullptr, 0, nullptr);

			if (!found) continue;

			int32_t result = 0;
			OSError osError = 0;
			ssize_t sent = ::send(socket, pending.mBuffer, pending.mBufferSize, 0);
			if (sent >= 0)
			{
				result = static_cast<int32_t>(sent);
			}
			else
			{
				osError = errno;
				result = -1;
			}

			CompletionEntry &entry = entries[processedCount];
			entry.mContext = pending.mContext;
			entry.mType = AsyncIOType::Send;
			entry.mResult = result;
			entry.mOsError = osError;
			entry.mCompletionTime = 0;
			mStats.mTotalCompletions++;
			processedCount++;
		}
	}

	return processedCount;
}

// =============================================================================
// Helper Methods
// =============================================================================

bool KqueueAsyncIOProvider::RegisterSocketEvents(SocketHandle socket)
{
	// Register for read events only; EVFILT_WRITE added dynamically on SendAsync
	struct kevent ev;
	EV_SET(&ev, socket, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);

	return kevent(mKqueueFd, &ev, 1, nullptr, 0, nullptr) >= 0;
}

bool KqueueAsyncIOProvider::UnregisterSocketEvents(SocketHandle socket)
{
	// Delete read and write events
	struct kevent events[2];
	EV_SET(&events[0], socket, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
	EV_SET(&events[1], socket, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);

	// Ignore errors (socket might already be closed)
	kevent(mKqueueFd, events, 2, nullptr, 0, nullptr);
	return true;
}

// =============================================================================
// Information & Statistics
// =============================================================================

const ProviderInfo &KqueueAsyncIOProvider::GetInfo() const { return mInfo; }

ProviderStats KqueueAsyncIOProvider::GetStats() const { return mStats; }

const char *KqueueAsyncIOProvider::GetLastError() const
{
	return mLastError.c_str();
}

// =============================================================================
// Factory Function
// =============================================================================

std::unique_ptr<AsyncIOProvider> CreateKqueueProvider()
{
	return std::unique_ptr<AsyncIOProvider>(new KqueueAsyncIOProvider());
}

} // namespace BSD
} // namespace AsyncIO
} // namespace Network

#endif // __APPLE__
