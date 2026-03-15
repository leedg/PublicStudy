// epoll-based AsyncIOProvider implementation

#ifdef __linux__

#include "EpollAsyncIOProvider.h"
#include "Utils/Logger.h"
#include "PlatformDetect.h"
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

namespace Network
{
namespace AsyncIO
{
namespace Linux
{
// =============================================================================
// Constructor & Destructor
// =============================================================================

EpollAsyncIOProvider::EpollAsyncIOProvider()
	: mEpollFd(-1), mInfo{}, mStats{}, mMaxConcurrentOps(0), mInitialized(false)
{
}

EpollAsyncIOProvider::~EpollAsyncIOProvider()
{
	// Ensure resources are released
	Shutdown();
}

// =============================================================================
// Lifecycle Management
// =============================================================================

AsyncIOError EpollAsyncIOProvider::Initialize(size_t queueDepth,
												  size_t maxConcurrent)
{
	if (mInitialized)
		return AsyncIOError::AlreadyInitialized;

	// Create epoll file descriptor with close-on-exec
	mEpollFd = epoll_create1(EPOLL_CLOEXEC);
	if (mEpollFd < 0)
	{
		mLastError = "epoll_create1 failed";
		return AsyncIOError::OperationFailed;
	}

	mMaxConcurrentOps = maxConcurrent;

	// Initialize provider info
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

	// Close epoll file descriptor
	if (mEpollFd >= 0)
	{
		close(mEpollFd);
		mEpollFd = -1;
	}

	mPendingRecvOps.clear();
	mPendingSendOps.clear();
	mInitialized = false;
}

bool EpollAsyncIOProvider::IsInitialized() const { return mInitialized; }

// =============================================================================
// Socket Association
// =============================================================================

AsyncIOError EpollAsyncIOProvider::AssociateSocket(SocketHandle socket,
												   RequestContext context)
{
	if (!mInitialized)
		return AsyncIOError::NotInitialized;

	// Register socket with epoll for read + error events
	struct epoll_event ev;
	ev.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
	ev.data.fd = socket;

	if (epoll_ctl(mEpollFd, EPOLL_CTL_ADD, socket, &ev) < 0)
	{
		mLastError = "epoll_ctl EPOLL_CTL_ADD failed";
		Utils::Logger::Error("EpollAsyncIOProvider::AssociateSocket - epoll_ctl EPOLL_CTL_ADD failed: " + std::string(strerror(errno)));
		return AsyncIOError::OperationFailed;
	}

	return AsyncIOError::Success;
}

// =============================================================================
// Buffer Management
// =============================================================================

int64_t EpollAsyncIOProvider::RegisterBuffer(const void *ptr, size_t size)
{
	// epoll doesn't support pre-registered buffers (no-op)
	return -1;
}

AsyncIOError EpollAsyncIOProvider::UnregisterBuffer(int64_t bufferId)
{
	// Not supported on epoll
	return AsyncIOError::PlatformNotSupported;
}

// =============================================================================
// Async I/O Operations
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

	// Dynamically add EPOLLOUT so we get notified when socket is writable
	struct epoll_event ev;
	ev.events = EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
	ev.data.fd = socket;
	if (epoll_ctl(mEpollFd, EPOLL_CTL_MOD, socket, &ev) < 0)
	{
		Utils::Logger::Error("EpollAsyncIOProvider::SendAsync - epoll_ctl EPOLL_CTL_MOD failed: " + std::string(strerror(errno)));
		mPendingSendOps.erase(socket);
		mStats.mPendingRequests--;
		return AsyncIOError::OperationFailed;
	}

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

	// Store pending operation (caller manages buffer)
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

AsyncIOError EpollAsyncIOProvider::FlushRequests()
{
	// epoll doesn't support batch processing (no-op)
	if (!mInitialized)
		return AsyncIOError::NotInitialized;

	return AsyncIOError::Success;
}

// =============================================================================
// Completion Processing
// =============================================================================

int EpollAsyncIOProvider::ProcessCompletions(CompletionEntry *entries,
											 size_t maxEntries, int timeoutMs)
{
	if (!mInitialized)
		return static_cast<int>(AsyncIOError::NotInitialized);
	if (!entries || maxEntries == 0 || mEpollFd < 0)
		return static_cast<int>(AsyncIOError::InvalidParameter);

	// Poll for events
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
		uint32_t evFlags = events[i].events;

		// Handle error / hangup events
		if (evFlags & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
		{
			// Find any pending op context for this socket for disconnect reporting
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
				entry.mOsError = (evFlags & EPOLLERR) ? EIO : 0;
				entry.mCompletionTime = 0;
				mStats.mTotalCompletions++;
				processedCount++;
			}
			continue;
		}

		// Handle EPOLLIN (readable)
		if (evFlags & EPOLLIN)
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

			if (found)
			{
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
					}
					else
					{
						osError = errno;
						result = -1;
						CompletionEntry &entry = entries[processedCount];
						entry.mContext = pending.mContext;
						entry.mType = AsyncIOType::Recv;
						entry.mResult = result;
						entry.mOsError = osError;
						entry.mCompletionTime = 0;
						mStats.mTotalCompletions++;
						processedCount++;
					}
					continue;
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
		}

		// Handle EPOLLOUT (writable) - execute send then remove EPOLLOUT
		if ((evFlags & EPOLLOUT) && processedCount < static_cast<int>(maxEntries))
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

			if (found)
			{
				// Remove EPOLLOUT after consuming send op
				struct epoll_event ev;
				ev.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
				ev.data.fd = socket;
				if (epoll_ctl(mEpollFd, EPOLL_CTL_MOD, socket, &ev) < 0)
				{
					Utils::Logger::Error("EpollAsyncIOProvider::ProcessCompletions - epoll_ctl EPOLL_CTL_MOD (found) failed: " + std::string(strerror(errno)));
				}

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
			else
			{
				// No pending send op - remove EPOLLOUT to avoid busy loop
				struct epoll_event ev;
				ev.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
				ev.data.fd = socket;
				epoll_ctl(mEpollFd, EPOLL_CTL_MOD, socket, &ev);
			}
		}
	}

	return processedCount;
}

// =============================================================================
// Information & Statistics
// =============================================================================

const ProviderInfo &EpollAsyncIOProvider::GetInfo() const { return mInfo; }

ProviderStats EpollAsyncIOProvider::GetStats() const { return mStats; }

const char *EpollAsyncIOProvider::GetLastError() const
{
	return mLastError.c_str();
}

// =============================================================================
// Factory Function
// =============================================================================

std::unique_ptr<AsyncIOProvider> CreateEpollProvider()
{
	return std::unique_ptr<AsyncIOProvider>(new EpollAsyncIOProvider());
}

} // namespace Linux
} // namespace AsyncIO
} // namespace Network

#endif // __linux__
