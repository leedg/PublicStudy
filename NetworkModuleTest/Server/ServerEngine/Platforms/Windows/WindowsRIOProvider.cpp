// English: Windows RIO AsyncIOProvider implementation
// 한글: Windows RIO AsyncIOProvider 구현

#ifdef _WIN32

#include "RIOAsyncIOProvider.h"
#include <vector>

namespace Network
{
namespace AsyncIO
{
namespace Windows
{

// =============================================================================
// English: Constructor & Destructor
// 한글: 생성자 및 소멸자
// =============================================================================

RIOAsyncIOProvider::RIOAsyncIOProvider()
	: mCompletionQueue(RIO_INVALID_CQ), mMaxConcurrentOps(0), mNextBufferId(1),
	  mInitialized(false)
{
	std::memset(&mInfo, 0, sizeof(mInfo));
	std::memset(&mStats, 0, sizeof(mStats));

	mInfo.mPlatformType = PlatformType::RIO;
	mInfo.mName = "RIO";
	mInfo.mCapabilities = 0;
	mInfo.mSupportsBufferReg = true;
	mInfo.mSupportsBatching = true;
	mInfo.mSupportsZeroCopy = true;

	mPfnRIOCloseCompletionQueue = nullptr;
	mPfnRIOCreateCompletionQueue = nullptr;
	mPfnRIOCreateRequestQueue = nullptr;
	mPfnRIODequeueCompletion = nullptr;
	mPfnRIONotify = nullptr;
	mPfnRIORegisterBuffer = nullptr;
	mPfnRIODeregisterBuffer = nullptr;
	mPfnRIOSend = nullptr;
	mPfnRIORecv = nullptr;
}

RIOAsyncIOProvider::~RIOAsyncIOProvider()
{
	Shutdown();
}

// =============================================================================
// English: RIO Function Loading
// 한글: RIO 함수 로딩
// =============================================================================

bool RIOAsyncIOProvider::LoadRIOFunctions()
{
	SOCKET tempSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (tempSocket == INVALID_SOCKET)
	{
		mLastError = "Failed to create temporary socket";
		return false;
	}

	GUID functionTableId = WSAID_MULTIPLE_RIO;
	RIO_EXTENSION_FUNCTION_TABLE rioFunctionTable;
	DWORD dwBytes = 0;

	int result = WSAIoctl(tempSocket, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER,
						  &functionTableId, sizeof(GUID),
						  &rioFunctionTable, sizeof(rioFunctionTable),
						  &dwBytes, nullptr, nullptr);

	closesocket(tempSocket);

	if (result == SOCKET_ERROR)
	{
		mLastError = "RIO not supported on this system";
		return false;
	}

	mPfnRIOCloseCompletionQueue = reinterpret_cast<PfnRIOCloseCompletionQueue>(
		rioFunctionTable.RIOCloseCompletionQueue);
	mPfnRIOCreateCompletionQueue = reinterpret_cast<PfnRIOCreateCompletionQueue>(
		rioFunctionTable.RIOCreateCompletionQueue);
	mPfnRIOCreateRequestQueue = reinterpret_cast<PfnRIOCreateRequestQueue>(
		rioFunctionTable.RIOCreateRequestQueue);
	mPfnRIODequeueCompletion = reinterpret_cast<PfnRIODequeueCompletion>(
		rioFunctionTable.RIODequeueCompletion);
	mPfnRIONotify = reinterpret_cast<PfnRIONotify>(
		rioFunctionTable.RIONotify);
	mPfnRIORegisterBuffer = reinterpret_cast<PfnRIORegisterBuffer>(
		rioFunctionTable.RIORegisterBuffer);
	mPfnRIODeregisterBuffer = reinterpret_cast<PfnRIODeregisterBuffer>(
		rioFunctionTable.RIODeregisterBuffer);
	mPfnRIOSend = reinterpret_cast<PfnRIOSend>(
		rioFunctionTable.RIOSend);
	mPfnRIORecv = reinterpret_cast<PfnRIORecv>(
		rioFunctionTable.RIOReceive);

	return true;
}

// =============================================================================
// English: Lifecycle Management
// 한글: 생명주기 관리
// =============================================================================

AsyncIOError RIOAsyncIOProvider::Initialize(size_t queueDepth,
											size_t maxConcurrent)
{
	if (mInitialized)
	{
		mLastError = "Already initialized";
		return AsyncIOError::AlreadyInitialized;
	}

	if (!LoadRIOFunctions())
	{
		return AsyncIOError::PlatformNotSupported;
	}

	RIO_NOTIFICATION_COMPLETION notificationCompletion;
	notificationCompletion.Type = RIO_EVENT_COMPLETION;
	notificationCompletion.Event.EventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	notificationCompletion.Event.NotifyReset = TRUE;

	if (!notificationCompletion.Event.EventHandle)
	{
		mLastError = "Failed to create event handle";
		return AsyncIOError::OperationFailed;
	}

	mCompletionQueue = mPfnRIOCreateCompletionQueue(
		static_cast<DWORD>(queueDepth), &notificationCompletion);

	if (mCompletionQueue == RIO_INVALID_CQ)
	{
		CloseHandle(notificationCompletion.Event.EventHandle);
		mLastError = "Failed to create RIO completion queue";
		return AsyncIOError::OperationFailed;
	}

	mMaxConcurrentOps = maxConcurrent;
	mInfo.mMaxQueueDepth = queueDepth;
	mInfo.mMaxConcurrentReq = maxConcurrent;
	mInitialized = true;

	return AsyncIOError::Success;
}

void RIOAsyncIOProvider::Shutdown()
{
	if (!mInitialized)
	{
		return;
	}

	{
		std::lock_guard<std::mutex> lock(mMutex);
		for (auto& entry : mRegisteredBuffers)
		{
			if (mPfnRIODeregisterBuffer)
			{
				mPfnRIODeregisterBuffer(entry.second.mRioBufferId);
			}
		}
		mRegisteredBuffers.clear();
		mRequestQueues.clear();
	}

	if (mCompletionQueue != RIO_INVALID_CQ && mPfnRIOCloseCompletionQueue)
	{
		mPfnRIOCloseCompletionQueue(mCompletionQueue);
		mCompletionQueue = RIO_INVALID_CQ;
	}

	mInitialized = false;
}

bool RIOAsyncIOProvider::IsInitialized() const
{
	return mInitialized;
}

// =============================================================================
// English: Buffer Management
// 한글: 버퍼 관리
// =============================================================================

int64_t RIOAsyncIOProvider::RegisterBuffer(const void *ptr, size_t size)
{
	if (!mInitialized || !ptr || size == 0)
	{
		return -1;
	}

	RIO_BUFFERID rioBufferId = mPfnRIORegisterBuffer(
		const_cast<PCHAR>(static_cast<const char*>(ptr)),
		static_cast<DWORD>(size));

	if (rioBufferId == RIO_INVALID_BUFFERID)
	{
		mLastError = "Failed to register buffer";
		return -1;
	}

	std::lock_guard<std::mutex> lock(mMutex);
	int64_t bufferId = mNextBufferId++;

	RegisteredBufferEntry entry;
	entry.mRioBufferId = rioBufferId;
	entry.mBufferPtr = const_cast<void*>(ptr);
	entry.mBufferSize = static_cast<uint32_t>(size);

	mRegisteredBuffers[bufferId] = entry;

	return bufferId;
}

AsyncIOError RIOAsyncIOProvider::UnregisterBuffer(int64_t bufferId)
{
	if (!mInitialized)
	{
		return AsyncIOError::NotInitialized;
	}

	std::lock_guard<std::mutex> lock(mMutex);
	auto it = mRegisteredBuffers.find(bufferId);
	if (it == mRegisteredBuffers.end())
	{
		mLastError = "Buffer not found";
		return AsyncIOError::InvalidParameter;
	}

	if (mPfnRIODeregisterBuffer(it->second.mRioBufferId) != 0)
	{
		mLastError = "Failed to deregister buffer";
		return AsyncIOError::OperationFailed;
	}

	mRegisteredBuffers.erase(it);
	return AsyncIOError::Success;
}

// =============================================================================
// English: Async I/O Requests
// 한글: 비동기 I/O 요청
// =============================================================================

AsyncIOError RIOAsyncIOProvider::SendAsync(SocketHandle socket,
										   const void *buffer, size_t size,
										   RequestContext context,
										   uint32_t flags)
{
	if (!mInitialized)
	{
		mLastError = "Not initialized";
		return AsyncIOError::NotInitialized;
	}

	RIO_RQ requestQueue;
	{
		std::lock_guard<std::mutex> lock(mMutex);
		auto it = mRequestQueues.find(socket);
		if (it == mRequestQueues.end())
		{
			requestQueue = mPfnRIOCreateRequestQueue(
				socket,
				static_cast<DWORD>(mMaxConcurrentOps),
				static_cast<DWORD>(mMaxConcurrentOps),
				mCompletionQueue);

			if (requestQueue == RIO_INVALID_RQ)
			{
				mLastError = "Failed to create request queue";
				return AsyncIOError::OperationFailed;
			}

			mRequestQueues[socket] = requestQueue;
		}
		else
		{
			requestQueue = it->second;
		}
	}

	RIO_BUF rioBuffer;
	rioBuffer.BufferId = RIO_INVALID_BUFFERID;
	rioBuffer.Offset = static_cast<ULONG>(reinterpret_cast<ULONG_PTR>(buffer));
	rioBuffer.Length = static_cast<ULONG>(size);

	if (!mPfnRIOSend(requestQueue, &rioBuffer, 1, flags,
					 reinterpret_cast<void*>(context)))
	{
		mLastError = "RIOSend failed";
		mStats.mErrorCount++;
		return AsyncIOError::OperationFailed;
	}

	mStats.mTotalRequests++;
	mStats.mPendingRequests++;

	return AsyncIOError::Success;
}

AsyncIOError RIOAsyncIOProvider::RecvAsync(SocketHandle socket, void *buffer,
										   size_t size, RequestContext context,
										   uint32_t flags)
{
	if (!mInitialized)
	{
		mLastError = "Not initialized";
		return AsyncIOError::NotInitialized;
	}

	RIO_RQ requestQueue;
	{
		std::lock_guard<std::mutex> lock(mMutex);
		auto it = mRequestQueues.find(socket);
		if (it == mRequestQueues.end())
		{
			requestQueue = mPfnRIOCreateRequestQueue(
				socket,
				static_cast<DWORD>(mMaxConcurrentOps),
				static_cast<DWORD>(mMaxConcurrentOps),
				mCompletionQueue);

			if (requestQueue == RIO_INVALID_RQ)
			{
				mLastError = "Failed to create request queue";
				return AsyncIOError::OperationFailed;
			}

			mRequestQueues[socket] = requestQueue;
		}
		else
		{
			requestQueue = it->second;
		}
	}

	RIO_BUF rioBuffer;
	rioBuffer.BufferId = RIO_INVALID_BUFFERID;
	rioBuffer.Offset = static_cast<ULONG>(reinterpret_cast<ULONG_PTR>(buffer));
	rioBuffer.Length = static_cast<ULONG>(size);

	if (!mPfnRIORecv(requestQueue, &rioBuffer, 1, flags,
					 reinterpret_cast<void*>(context)))
	{
		mLastError = "RIORecv failed";
		mStats.mErrorCount++;
		return AsyncIOError::OperationFailed;
	}

	mStats.mTotalRequests++;
	mStats.mPendingRequests++;

	return AsyncIOError::Success;
}

AsyncIOError RIOAsyncIOProvider::FlushRequests()
{
	if (!mInitialized)
	{
		return AsyncIOError::NotInitialized;
	}

	if (mCompletionQueue != RIO_INVALID_CQ && mPfnRIONotify)
	{
		mPfnRIONotify(mCompletionQueue);
	}

	return AsyncIOError::Success;
}

// =============================================================================
// English: Completion Processing
// 한글: 완료 처리
// =============================================================================

int RIOAsyncIOProvider::ProcessCompletions(CompletionEntry *entries,
										   size_t maxEntries, int timeoutMs)
{
	if (!mInitialized)
	{
		mLastError = "Not initialized";
		return static_cast<int>(AsyncIOError::NotInitialized);
	}

	if (!entries || maxEntries == 0)
	{
		mLastError = "Invalid parameters";
		return static_cast<int>(AsyncIOError::InvalidParameter);
	}

	std::vector<RIORESULT> rioResults(maxEntries);

	ULONG numResults = mPfnRIODequeueCompletion(
		mCompletionQueue, rioResults.data(), static_cast<ULONG>(maxEntries));

	if (numResults == RIO_CORRUPT_CQ)
	{
		mLastError = "RIO completion queue corrupted";
		mStats.mErrorCount++;
		return static_cast<int>(AsyncIOError::OperationFailed);
	}

	for (ULONG i = 0; i < numResults; ++i)
	{
		entries[i].mContext = static_cast<RequestContext>(rioResults[i].RequestContext);
		entries[i].mType = AsyncIOType::Send;
		entries[i].mResult = static_cast<int32_t>(rioResults[i].BytesTransferred);
		entries[i].mOsError = static_cast<OSError>(rioResults[i].Status);
		entries[i].mCompletionTime = 0;

		mStats.mTotalCompletions++;
		if (mStats.mPendingRequests > 0)
		{
			mStats.mPendingRequests--;
		}
	}

	return static_cast<int>(numResults);
}

// =============================================================================
// English: Information & Statistics
// 한글: 정보 및 통계
// =============================================================================

const ProviderInfo &RIOAsyncIOProvider::GetInfo() const
{
	return mInfo;
}

ProviderStats RIOAsyncIOProvider::GetStats() const
{
	return mStats;
}

const char *RIOAsyncIOProvider::GetLastError() const
{
	return mLastError.c_str();
}

// =============================================================================
// English: Factory function
// 한글: 팩토리 함수
// =============================================================================

std::unique_ptr<AsyncIOProvider> CreateRIOProvider()
{
	return std::make_unique<RIOAsyncIOProvider>();
}

} // namespace Windows
} // namespace AsyncIO
} // namespace Network

#endif // _WIN32
