// Windows IOCP AsyncIOProvider implementation

#ifdef _WIN32

#include "IocpAsyncIOProvider.h"
#include "Network/Core/Session.h"
#include <chrono>

namespace Network
{
namespace AsyncIO
{
namespace Windows
{

// =============================================================================
// Constructor & Destructor
// =============================================================================

IocpAsyncIOProvider::IocpAsyncIOProvider()
	: mCompletionPort(INVALID_HANDLE_VALUE), mMaxConcurrentOps(0),
	  mInitialized(false), mShuttingDown(false)
{
	std::memset(&mInfo, 0, sizeof(mInfo));
	std::memset(&mStats, 0, sizeof(mStats));

	mInfo.mPlatformType = PlatformType::IOCP;
	mInfo.mName = "IOCP";
	mInfo.mCapabilities = 0;
	mInfo.mSupportsBufferReg = false;
	mInfo.mSupportsBatching = false;
	mInfo.mSupportsZeroCopy = false;
}

IocpAsyncIOProvider::~IocpAsyncIOProvider()
{
	Shutdown();
}

// =============================================================================
// Lifecycle Management
// =============================================================================

AsyncIOError IocpAsyncIOProvider::Initialize(size_t queueDepth,
											 size_t maxConcurrent)
{
	if (mInitialized.load(std::memory_order_acquire))
	{
		mLastError = "Already initialized";
		return AsyncIOError::AlreadyInitialized;
	}

	mShuttingDown.store(false, std::memory_order_release);

	// Create IOCP
	DWORD concurrentThreads = maxConcurrent > 0 ? static_cast<DWORD>(maxConcurrent) : 0;
	mCompletionPort = CreateIoCompletionPort(
		INVALID_HANDLE_VALUE, nullptr, 0, concurrentThreads);

	if (!mCompletionPort || mCompletionPort == INVALID_HANDLE_VALUE)
	{
		DWORD error = ::GetLastError();
		mLastError = std::string("Failed to create IOCP: ") + std::to_string(error);
		return AsyncIOError::OperationFailed;
	}

	mMaxConcurrentOps = maxConcurrent;
	mInfo.mMaxQueueDepth = queueDepth;
	mInfo.mMaxConcurrentReq = maxConcurrent;
	mInitialized.store(true, std::memory_order_release);

	return AsyncIOError::Success;
}

void IocpAsyncIOProvider::Shutdown()
{
	bool expected = true;
	if (!mInitialized.compare_exchange_strong(
			expected, false, std::memory_order_acq_rel))
	{
		return;
	}
	mShuttingDown.store(true, std::memory_order_release);

	if (mCompletionPort && mCompletionPort != INVALID_HANDLE_VALUE)
	{
		CloseHandle(mCompletionPort);
		mCompletionPort = INVALID_HANDLE_VALUE;
	}

	std::lock_guard<std::mutex> lock(mMutex);
	mPendingRecvOps.clear();
	mPendingSendOps.clear();

}

bool IocpAsyncIOProvider::IsInitialized() const
{
	return mInitialized.load(std::memory_order_acquire);
}

// =============================================================================
// Socket Association
// =============================================================================

AsyncIOError IocpAsyncIOProvider::AssociateSocket(SocketHandle socket,
												  RequestContext context)
{
	if (!mInitialized.load(std::memory_order_acquire) ||
		mShuttingDown.load(std::memory_order_acquire))
	{
		mLastError = "Not initialized";
		return AsyncIOError::NotInitialized;
	}

	// Associate the socket with the IOCP completion port
	// The completionKey (context) is typically the ConnectionId,
	// which will be returned in GetQueuedCompletionStatus completionKey parameter.
	HANDLE result = CreateIoCompletionPort(
		reinterpret_cast<HANDLE>(socket),
		mCompletionPort,
		static_cast<ULONG_PTR>(context),
		0);

	if (!result)
	{
		DWORD error = ::GetLastError();
		mLastError = "CreateIoCompletionPort (associate) failed: " +
					 std::to_string(error);
		return AsyncIOError::OperationFailed;
	}

	return AsyncIOError::Success;
}

// =============================================================================
// Buffer Management
// =============================================================================

int64_t IocpAsyncIOProvider::RegisterBuffer(const void *ptr, size_t size)
{
	// IOCP doesn't need buffer registration
	return 0;
}

AsyncIOError IocpAsyncIOProvider::UnregisterBuffer(int64_t bufferId)
{
	// IOCP doesn't support buffer registration
	return AsyncIOError::Success;
}

// =============================================================================
// Async I/O Requests
// =============================================================================

AsyncIOError IocpAsyncIOProvider::SendAsync(SocketHandle socket,
											const void *buffer, size_t size,
											RequestContext context,
											uint32_t flags)
{
	(void)flags;
	if (!mInitialized.load(std::memory_order_acquire) ||
		mShuttingDown.load(std::memory_order_acquire))
	{
		mLastError = "Not initialized";
		return AsyncIOError::NotInitialized;
	}

	if (!buffer || size == 0)
	{
		mLastError = "Invalid buffer";
		return AsyncIOError::InvalidBuffer;
	}

	auto op = std::make_unique<PendingOperation>();
	std::memset(&op->mOverlapped, 0, sizeof(OVERLAPPED));
	op->mBuffer = std::make_unique<uint8_t[]>(size);
	std::memcpy(op->mBuffer.get(), buffer, size);
	op->mWsaBuffer.buf = reinterpret_cast<char *>(op->mBuffer.get());
	op->mWsaBuffer.len = static_cast<ULONG>(size);
	op->mContext = context;
	op->mType = AsyncIOType::Send;
	op->mSocket = socket;
	OVERLAPPED *opKey = &op->mOverlapped;

	std::lock_guard<std::mutex> lock(mMutex);
	if (!mInitialized.load(std::memory_order_acquire) ||
		mShuttingDown.load(std::memory_order_acquire))
	{
		mLastError = "Provider is shutting down";
		return AsyncIOError::NotInitialized;
	}

	auto [it, inserted] = mPendingSendOps.emplace(opKey, std::move(op));
	if (!inserted)
	{
		mLastError = "Duplicate pending send OVERLAPPED key";
		mStats.mErrorCount++;
		return AsyncIOError::OperationFailed;
	}

	mStats.mTotalRequests++;
	mStats.mPendingRequests++;

	DWORD bytesSent = 0;
	int result = WSASend(socket, &it->second->mWsaBuffer, 1, &bytesSent, 0,
								opKey, nullptr);
	if (result == SOCKET_ERROR)
	{
		const int errorCode = WSAGetLastError();
		if (errorCode != WSA_IO_PENDING)
		{
			mPendingSendOps.erase(it);
			if (mStats.mPendingRequests > 0)
			{
				mStats.mPendingRequests--;
			}
			mStats.mErrorCount++;
			mLastError = "WSASend failed: " + std::to_string(errorCode);
			return AsyncIOError::OperationFailed;
		}
	}

	return AsyncIOError::Success;
}

AsyncIOError IocpAsyncIOProvider::RecvAsync(SocketHandle socket, void *buffer,
											size_t size, RequestContext context,
											uint32_t flags)
{
	(void)flags;
	if (!mInitialized.load(std::memory_order_acquire) ||
		mShuttingDown.load(std::memory_order_acquire))
	{
		mLastError = "Not initialized";
		return AsyncIOError::NotInitialized;
	}

	if (!buffer || size == 0)
	{
		mLastError = "Invalid buffer";
		return AsyncIOError::InvalidBuffer;
	}

	auto op = std::make_unique<PendingOperation>();
	std::memset(&op->mOverlapped, 0, sizeof(OVERLAPPED));
	op->mBuffer = std::make_unique<uint8_t[]>(size);
	op->mWsaBuffer.buf = reinterpret_cast<char *>(op->mBuffer.get());
	op->mWsaBuffer.len = static_cast<ULONG>(size);
	op->mContext = context;
	op->mType = AsyncIOType::Recv;
	op->mSocket = socket;
	OVERLAPPED *opKey = &op->mOverlapped;

	std::lock_guard<std::mutex> lock(mMutex);
	if (!mInitialized.load(std::memory_order_acquire) ||
		mShuttingDown.load(std::memory_order_acquire))
	{
		mLastError = "Provider is shutting down";
		return AsyncIOError::NotInitialized;
	}

	auto [it, inserted] = mPendingRecvOps.emplace(opKey, std::move(op));
	if (!inserted)
	{
		mLastError = "Duplicate pending recv OVERLAPPED key";
		mStats.mErrorCount++;
		return AsyncIOError::OperationFailed;
	}

	mStats.mTotalRequests++;
	mStats.mPendingRequests++;

	DWORD bytesRecv = 0;
	DWORD dwFlags = 0;
	int result = WSARecv(socket, &it->second->mWsaBuffer, 1, &bytesRecv, &dwFlags,
								opKey, nullptr);
	if (result == SOCKET_ERROR)
	{
		const int errorCode = WSAGetLastError();
		if (errorCode != WSA_IO_PENDING)
		{
			mPendingRecvOps.erase(it);
			if (mStats.mPendingRequests > 0)
			{
				mStats.mPendingRequests--;
			}
			mStats.mErrorCount++;
			mLastError = "WSARecv failed: " + std::to_string(errorCode);
			return AsyncIOError::OperationFailed;
		}
	}

	return AsyncIOError::Success;
}

AsyncIOError IocpAsyncIOProvider::FlushRequests()
{
	// IOCP executes immediately, no batching
	return AsyncIOError::Success;
}

// =============================================================================
// Completion Processing
// =============================================================================

int IocpAsyncIOProvider::ProcessCompletions(CompletionEntry *entries,
											size_t maxEntries, int timeoutMs)
{
	if (!mInitialized.load(std::memory_order_acquire))
	{
		mLastError = "Not initialized";
		return static_cast<int>(AsyncIOError::NotInitialized);
	}

	if (!entries || maxEntries == 0)
	{
		mLastError = "Invalid parameters";
		return static_cast<int>(AsyncIOError::InvalidParameter);
	}

	DWORD timeout = (timeoutMs < 0) ? INFINITE : static_cast<DWORD>(timeoutMs);
	int completionCount = 0;
	auto startTime = std::chrono::high_resolution_clock::now();

	for (size_t i = 0; i < maxEntries; ++i)
	{
		DWORD bytesTransferred = 0;
		ULONG_PTR completionKey = 0;
		OVERLAPPED *pOverlapped = nullptr;

		BOOL result = GetQueuedCompletionStatus(mCompletionPort, &bytesTransferred,
											&completionKey, &pOverlapped, timeout);

		if (!result && !pOverlapped)
		{
			break;
		}

		std::unique_ptr<PendingOperation> op;
		if (pOverlapped)
		{
			std::lock_guard<std::mutex> lock(mMutex);
			auto recvIt = mPendingRecvOps.find(pOverlapped);
			if (recvIt != mPendingRecvOps.end())
			{
				op = std::move(recvIt->second);
				mPendingRecvOps.erase(recvIt);
				if (mStats.mPendingRequests > 0)
				{
					mStats.mPendingRequests--;
				}
			}
			else
			{
				auto sendIt = mPendingSendOps.find(pOverlapped);
				if (sendIt != mPendingSendOps.end())
				{
					op = std::move(sendIt->second);
					mPendingSendOps.erase(sendIt);
					if (mStats.mPendingRequests > 0)
					{
						mStats.mPendingRequests--;
					}
				}
			}
		}

		if (op)
		{
			entries[completionCount].mContext = op->mContext;
			entries[completionCount].mType = op->mType;
		}
		else if (pOverlapped)
		{
			Network::Core::IOType ioType = Network::Core::IOType::Recv;
			if (!Network::Core::Session::TryResolveIOType(pOverlapped, ioType))
			{
				timeout = 0;
				continue;
			}

			entries[completionCount].mContext = static_cast<RequestContext>(completionKey);
			entries[completionCount].mType =
				(ioType == Network::Core::IOType::Recv) ? AsyncIOType::Recv
												 : AsyncIOType::Send;
		}
		else
		{
			timeout = 0;
			continue;
		}

		entries[completionCount].mResult =
			result ? static_cast<int32_t>(bytesTransferred) : -1;
		entries[completionCount].mOsError =
			static_cast<OSError>(result ? 0 : ::GetLastError());

		auto endTime = std::chrono::high_resolution_clock::now();
		auto duration =
			std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime);
		entries[completionCount].mCompletionTime = duration.count();

		{
			std::lock_guard<std::mutex> lock(mMutex);
			mStats.mTotalCompletions++;
		}
		completionCount++;
		timeout = 0;
	}

	return completionCount;
}

// =============================================================================
// Information & Statistics
// =============================================================================

const ProviderInfo &IocpAsyncIOProvider::GetInfo() const
{
	return mInfo;
}

ProviderStats IocpAsyncIOProvider::GetStats() const
{
	std::lock_guard<std::mutex> lock(mMutex);
	return mStats;
}

const char *IocpAsyncIOProvider::GetLastError() const
{
	return mLastError.c_str();
}

// =============================================================================
// Factory function
// =============================================================================

std::unique_ptr<AsyncIOProvider> CreateIocpProvider()
{
	return std::make_unique<IocpAsyncIOProvider>();
}

} // namespace Windows
} // namespace AsyncIO
} // namespace Network

#endif // _WIN32
