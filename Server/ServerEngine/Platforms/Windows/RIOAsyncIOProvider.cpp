// English: Windows RIO AsyncIOProvider implementation

#ifdef _WIN32

#include "RIOAsyncIOProvider.h"
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

namespace Network
{
namespace AsyncIO
{
namespace Windows
{

RIOAsyncIOProvider::RIOAsyncIOProvider()
	: mCompletionQueue(RIO_INVALID_CQ), mCompletionEvent(nullptr),
	  mMaxConcurrentOps(0), mNextBufferId(1), mInitialized(false),
	  mShuttingDown(false)
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

bool RIOAsyncIOProvider::LoadRIOFunctions()
{
	SOCKET tempSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0,
								 WSA_FLAG_REGISTERED_IO);
	if (tempSocket == INVALID_SOCKET)
	{
		mLastError = "Failed to create temporary RIO socket";
		return false;
	}

	GUID functionTableId = WSAID_MULTIPLE_RIO;
	RIO_EXTENSION_FUNCTION_TABLE rioFunctionTable;
	DWORD bytes = 0;

	int result = WSAIoctl(tempSocket, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER,
						  &functionTableId, sizeof(functionTableId),
						  &rioFunctionTable, sizeof(rioFunctionTable), &bytes,
						  nullptr, nullptr);

	closesocket(tempSocket);

	if (result == SOCKET_ERROR)
	{
		mLastError = "RIO not supported on this system";
		return false;
	}

	mPfnRIOCloseCompletionQueue = rioFunctionTable.RIOCloseCompletionQueue;
	mPfnRIOCreateCompletionQueue = rioFunctionTable.RIOCreateCompletionQueue;
	mPfnRIOCreateRequestQueue = rioFunctionTable.RIOCreateRequestQueue;
	mPfnRIODequeueCompletion = rioFunctionTable.RIODequeueCompletion;
	mPfnRIONotify = rioFunctionTable.RIONotify;
	mPfnRIORegisterBuffer = rioFunctionTable.RIORegisterBuffer;
	mPfnRIODeregisterBuffer = rioFunctionTable.RIODeregisterBuffer;
	mPfnRIOSend = rioFunctionTable.RIOSend;
	mPfnRIORecv = rioFunctionTable.RIOReceive;

	return true;
}

AsyncIOError RIOAsyncIOProvider::Initialize(size_t queueDepth,
									 size_t maxConcurrent)
{
	if (mInitialized.load(std::memory_order_acquire))
	{
		mLastError = "Already initialized";
		return AsyncIOError::AlreadyInitialized;
	}
	mShuttingDown.store(false, std::memory_order_release);

	if (!LoadRIOFunctions())
	{
		return AsyncIOError::PlatformNotSupported;
	}

	mCompletionEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (!mCompletionEvent)
	{
		mLastError = "Failed to create RIO completion event";
		return AsyncIOError::OperationFailed;
	}

	RIO_NOTIFICATION_COMPLETION notificationCompletion;
	std::memset(&notificationCompletion, 0, sizeof(notificationCompletion));
	notificationCompletion.Type = RIO_EVENT_COMPLETION;
	notificationCompletion.Event.EventHandle = mCompletionEvent;
	notificationCompletion.Event.NotifyReset = TRUE;

	mCompletionQueue =
		mPfnRIOCreateCompletionQueue(static_cast<DWORD>(queueDepth),
								 &notificationCompletion);
	if (mCompletionQueue == RIO_INVALID_CQ)
	{
		mLastError = "Failed to create RIO completion queue";
		CloseHandle(mCompletionEvent);
		mCompletionEvent = nullptr;
		return AsyncIOError::OperationFailed;
	}

	mMaxConcurrentOps = (maxConcurrent > 0) ? maxConcurrent : 128;
	mInfo.mMaxQueueDepth = queueDepth;
	mInfo.mMaxConcurrentReq = mMaxConcurrentOps;
	mInitialized.store(true, std::memory_order_release);

	return AsyncIOError::Success;
}

void RIOAsyncIOProvider::CleanupPendingOperation(PendingOperation &op)
{
	if (op.mRioBufferId != RIO_INVALID_BUFFERID && mPfnRIODeregisterBuffer)
	{
		mPfnRIODeregisterBuffer(op.mRioBufferId);
		op.mRioBufferId = RIO_INVALID_BUFFERID;
	}
}

void RIOAsyncIOProvider::Shutdown()
{
	bool expected = true;
	if (!mInitialized.compare_exchange_strong(
			expected, false, std::memory_order_acq_rel))
	{
		return;
	}
	mShuttingDown.store(true, std::memory_order_release);

	{
		std::lock_guard<std::mutex> lock(mMutex);

		for (auto &kv : mPendingOps)
		{
			if (kv.second)
			{
				CleanupPendingOperation(*kv.second);
			}
		}
		mPendingOps.clear();

		for (auto &entry : mRegisteredBuffers)
		{
			if (entry.second.mRioBufferId != RIO_INVALID_BUFFERID &&
				mPfnRIODeregisterBuffer)
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

	if (mCompletionEvent)
	{
		CloseHandle(mCompletionEvent);
		mCompletionEvent = nullptr;
	}

}

bool RIOAsyncIOProvider::IsInitialized() const
{
	return mInitialized.load(std::memory_order_acquire);
}

AsyncIOError RIOAsyncIOProvider::GetOrCreateRequestQueue(
	SocketHandle socket, RIO_RQ &outQueue, RequestContext contextForSocket)
{
	if (!mInitialized.load(std::memory_order_acquire) ||
		mShuttingDown.load(std::memory_order_acquire))
	{
		return AsyncIOError::NotInitialized;
	}

	if (socket == INVALID_SOCKET)
	{
		mLastError = "Invalid socket";
		return AsyncIOError::InvalidSocket;
	}

	std::lock_guard<std::mutex> lock(mMutex);

	auto it = mRequestQueues.find(socket);
	if (it != mRequestQueues.end())
	{
		outQueue = it->second;
		return AsyncIOError::Success;
	}

	// English: Per-socket queue limits must fit the shared CQ capacity.
	// Keep these small because this engine posts at most one recv and one send
	// per socket at a time.
	const ULONG maxOutstandingReceive = 1;
	const ULONG maxOutstandingSend = 1;
	RIO_RQ requestQueue = mPfnRIOCreateRequestQueue(
		socket, maxOutstandingReceive, 1, maxOutstandingSend, 1, mCompletionQueue,
		mCompletionQueue, reinterpret_cast<void *>(static_cast<uintptr_t>(contextForSocket)));

	if (requestQueue == RIO_INVALID_RQ)
	{
		mLastError = "Failed to create RIO request queue (WSA: " +
					 std::to_string(WSAGetLastError()) + ")";
		return AsyncIOError::OperationFailed;
	}

	mRequestQueues.emplace(socket, requestQueue);
	outQueue = requestQueue;
	return AsyncIOError::Success;
}

AsyncIOError RIOAsyncIOProvider::AssociateSocket(SocketHandle socket,
									 RequestContext context)
{
	RIO_RQ queue = RIO_INVALID_RQ;
	return GetOrCreateRequestQueue(socket, queue, context);
}

int64_t RIOAsyncIOProvider::RegisterBuffer(const void *ptr, size_t size)
{
	if (!mInitialized.load(std::memory_order_acquire) ||
		mShuttingDown.load(std::memory_order_acquire) || !ptr || size == 0)
	{
		return -1;
	}

	RIO_BUFFERID rioBufferId = mPfnRIORegisterBuffer(
		const_cast<PCHAR>(reinterpret_cast<const char *>(ptr)),
		static_cast<DWORD>(size));

	if (rioBufferId == RIO_INVALID_BUFFERID)
	{
		mLastError = "Failed to register buffer";
		return -1;
	}

	std::lock_guard<std::mutex> lock(mMutex);
	const int64_t bufferId = mNextBufferId++;
	mRegisteredBuffers[bufferId] =
		RegisteredBufferEntry{rioBufferId, const_cast<void *>(ptr),
								 static_cast<uint32_t>(size)};

	return bufferId;
}

AsyncIOError RIOAsyncIOProvider::UnregisterBuffer(int64_t bufferId)
{
	if (!mInitialized.load(std::memory_order_acquire) ||
		mShuttingDown.load(std::memory_order_acquire))
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

	if (it->second.mRioBufferId != RIO_INVALID_BUFFERID && mPfnRIODeregisterBuffer)
	{
		mPfnRIODeregisterBuffer(it->second.mRioBufferId);
	}

	mRegisteredBuffers.erase(it);
	return AsyncIOError::Success;
}

AsyncIOError RIOAsyncIOProvider::SendAsync(SocketHandle socket,
									  const void *buffer, size_t size,
									  RequestContext context,
									  uint32_t flags)
{
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

	RIO_RQ requestQueue = RIO_INVALID_RQ;
	AsyncIOError queueResult = GetOrCreateRequestQueue(socket, requestQueue, context);
	if (queueResult != AsyncIOError::Success)
	{
		return queueResult;
	}

	auto op = std::make_shared<PendingOperation>();
	op->mContext = context;
	op->mSocket = socket;
	op->mType = AsyncIOType::Send;
	op->mBufferSize = size;
	op->mOwnedBuffer.resize(size);
	std::memcpy(op->mOwnedBuffer.data(), buffer, size);
	op->mBufferPtr = op->mOwnedBuffer.data();

	op->mRioBufferId = mPfnRIORegisterBuffer(
		reinterpret_cast<PCHAR>(op->mBufferPtr), static_cast<DWORD>(size));
	if (op->mRioBufferId == RIO_INVALID_BUFFERID)
	{
		mLastError = "Failed to register send buffer";
		std::lock_guard<std::mutex> lock(mMutex);
		mStats.mErrorCount++;
		return AsyncIOError::OperationFailed;
	}

	RIO_BUF rioBuffer;
	rioBuffer.BufferId = op->mRioBufferId;
	rioBuffer.Offset = 0;
	rioBuffer.Length = static_cast<ULONG>(size);

	const uintptr_t opKey = static_cast<uintptr_t>(
		mNextOpId.fetch_add(1, std::memory_order_relaxed));
	op->mOpId = opKey;

	std::lock_guard<std::mutex> lock(mMutex);
	if (!mInitialized.load(std::memory_order_acquire) ||
		mShuttingDown.load(std::memory_order_acquire))
	{
		CleanupPendingOperation(*op);
		mLastError = "Provider is shutting down";
		return AsyncIOError::NotInitialized;
	}

	mPendingOps.emplace(opKey, op);
	mStats.mTotalRequests++;
	mStats.mPendingRequests++;

	if (!mPfnRIOSend(requestQueue, &rioBuffer, 1, flags,
					 reinterpret_cast<void *>(opKey)))
	{
		mPendingOps.erase(opKey);
		if (mStats.mPendingRequests > 0)
		{
			mStats.mPendingRequests--;
		}
		mStats.mErrorCount++;
		CleanupPendingOperation(*op);
		mLastError = "RIOSend failed";
		return AsyncIOError::OperationFailed;
	}

	return AsyncIOError::Success;
}

AsyncIOError RIOAsyncIOProvider::RecvAsync(SocketHandle socket, void *buffer,
									  size_t size,
									  RequestContext context,
									  uint32_t flags)
{
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

	RIO_RQ requestQueue = RIO_INVALID_RQ;
	AsyncIOError queueResult = GetOrCreateRequestQueue(socket, requestQueue, context);
	if (queueResult != AsyncIOError::Success)
	{
		return queueResult;
	}

	auto op = std::make_shared<PendingOperation>();
	op->mContext = context;
	op->mSocket = socket;
	op->mType = AsyncIOType::Recv;
	op->mBufferPtr = buffer;
	op->mBufferSize = size;

	op->mRioBufferId = mPfnRIORegisterBuffer(
		reinterpret_cast<PCHAR>(buffer), static_cast<DWORD>(size));
	if (op->mRioBufferId == RIO_INVALID_BUFFERID)
	{
		mLastError = "Failed to register recv buffer";
		std::lock_guard<std::mutex> lock(mMutex);
		mStats.mErrorCount++;
		return AsyncIOError::OperationFailed;
	}

	RIO_BUF rioBuffer;
	rioBuffer.BufferId = op->mRioBufferId;
	rioBuffer.Offset = 0;
	rioBuffer.Length = static_cast<ULONG>(size);

	const uintptr_t opKey = static_cast<uintptr_t>(
		mNextOpId.fetch_add(1, std::memory_order_relaxed));
	op->mOpId = opKey;

	std::lock_guard<std::mutex> lock(mMutex);
	if (!mInitialized.load(std::memory_order_acquire) ||
		mShuttingDown.load(std::memory_order_acquire))
	{
		CleanupPendingOperation(*op);
		mLastError = "Provider is shutting down";
		return AsyncIOError::NotInitialized;
	}

	mPendingOps.emplace(opKey, op);
	mStats.mTotalRequests++;
	mStats.mPendingRequests++;

	if (!mPfnRIORecv(requestQueue, &rioBuffer, 1, flags,
					 reinterpret_cast<void *>(opKey)))
	{
		mPendingOps.erase(opKey);
		if (mStats.mPendingRequests > 0)
		{
			mStats.mPendingRequests--;
		}
		mStats.mErrorCount++;
		CleanupPendingOperation(*op);
		mLastError = "RIORecv failed";
		return AsyncIOError::OperationFailed;
	}

	return AsyncIOError::Success;
}

AsyncIOError RIOAsyncIOProvider::FlushRequests()
{
	if (!mInitialized.load(std::memory_order_acquire))
	{
		return AsyncIOError::NotInitialized;
	}

	return AsyncIOError::Success;
}

int RIOAsyncIOProvider::ProcessCompletions(CompletionEntry *entries,
									 size_t maxEntries,
									 int timeoutMs)
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

	// RIO 알림 직렬화: 한 번에 한 스레드만 RIONotify + 이벤트 대기 수행
	// Serialize RIO notification: only one thread calls RIONotify + waits at a time
	std::unique_lock<std::mutex> notifyLock(mNotifyMutex, std::try_to_lock);
	if (!notifyLock.owns_lock())
	{
		// 다른 스레드가 이미 알림 대기 중 - 짧게 양보 후 0 반환
		// Another thread is already waiting for notification - yield briefly and return 0
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		return 0;
	}

	// RIO 이벤트 알림 활성화 (dequeue 전에 반드시 호출해야 함)
	// Arm RIO event notification (must be called before dequeue to avoid missed wakeups)
	bool notifyOk = (mPfnRIONotify(mCompletionQueue) != FALSE);
	if (!notifyOk)
	{
		DWORD err = ::GetLastError();
		if (err != 0)
		{
			// 실제 오류인 경우에만 에러 처리
			// Only treat as error when GetLastError is non-zero
			mLastError = "RIONotify failed: " + std::to_string(err);
			std::lock_guard<std::mutex> lock(mMutex);
			mStats.mErrorCount++;
			return static_cast<int>(AsyncIOError::OperationFailed);
		}
		// err == 0: 이미 알림이 대기 중이거나 이벤트가 이미 설정됨 → 즉시 dequeue
		// err == 0: notification already pending or event already set → dequeue immediately
	}
	else
	{
		// 완료 이벤트가 신호될 때까지 블로킹 대기 (timeoutMs 기반)
		// Block until completion event is signaled (timeoutMs-based)
		DWORD waitMs = (timeoutMs < 0) ? INFINITE : static_cast<DWORD>(timeoutMs);
		DWORD waitResult = WaitForSingleObject(mCompletionEvent, waitMs);
		if (waitResult == WAIT_TIMEOUT)
		{
			return 0;
		}
		if (waitResult != WAIT_OBJECT_0)
		{
			mLastError = "WaitForSingleObject failed: " + std::to_string(::GetLastError());
			return static_cast<int>(AsyncIOError::OperationFailed);
		}
	}

	// 이벤트 수신(또는 기존 완료 감지) 후 항목 dequeue
	// Dequeue completions after event signaled (or existing completions detected)
	std::vector<RIORESULT> rioResults(maxEntries);
	ULONG numResults = mPfnRIODequeueCompletion(
		mCompletionQueue, rioResults.data(), static_cast<ULONG>(maxEntries));

	if (numResults == RIO_CORRUPT_CQ)
	{
		mLastError = "RIO completion queue corrupted";
		std::lock_guard<std::mutex> lock(mMutex);
		mStats.mErrorCount++;
		return static_cast<int>(AsyncIOError::OperationFailed);
	}

	int completionCount = 0;
	for (ULONG i = 0; i < numResults && completionCount < static_cast<int>(maxEntries);
		 ++i)
	{
		uintptr_t opKey = static_cast<uintptr_t>(rioResults[i].RequestContext);
		std::shared_ptr<PendingOperation> op;
		{
			std::lock_guard<std::mutex> lock(mMutex);
			auto it = mPendingOps.find(opKey);
			if (it != mPendingOps.end())
			{
				op = it->second;
				mPendingOps.erase(it);
			}
		}

		if (!op)
		{
			continue;
		}

		// Verify shutdown status before accessing buffer
		// ProcessCompletions() starts with mShuttingDown check outside lock,
		// but Shutdown() may release buffers after that check.
		// Re-check here before dereferencing RequestContext (buffer pointer).
		if (mShuttingDown.load(std::memory_order_acquire))
		{
			continue;
		}

		entries[completionCount].mContext = op->mContext;
		entries[completionCount].mType = op->mType;
		entries[completionCount].mResult =
			static_cast<int32_t>(rioResults[i].BytesTransferred);
		entries[completionCount].mOsError =
			static_cast<OSError>(rioResults[i].Status);
		entries[completionCount].mCompletionTime = 0;

		if (rioResults[i].Status != NO_ERROR)
		{
			entries[completionCount].mResult = -1;
		}

		CleanupPendingOperation(*op);

		{
			std::lock_guard<std::mutex> lock(mMutex);
			mStats.mTotalCompletions++;
			if (mStats.mPendingRequests > 0)
			{
				mStats.mPendingRequests--;
			}
			if (entries[completionCount].mOsError != 0)
			{
				mStats.mErrorCount++;
			}
		}

		completionCount++;
	}

	return completionCount;
}

const ProviderInfo &RIOAsyncIOProvider::GetInfo() const
{
	return mInfo;
}

ProviderStats RIOAsyncIOProvider::GetStats() const
{
	std::lock_guard<std::mutex> lock(mMutex);
	return mStats;
}

const char *RIOAsyncIOProvider::GetLastError() const
{
	return mLastError.c_str();
}

std::unique_ptr<AsyncIOProvider> CreateRIOProvider()
{
	return std::make_unique<RIOAsyncIOProvider>();
}

} // namespace Windows
} // namespace AsyncIO
} // namespace Network

#endif // _WIN32
