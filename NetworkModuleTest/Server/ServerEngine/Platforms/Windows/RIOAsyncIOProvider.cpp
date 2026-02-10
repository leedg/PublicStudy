// English: Windows RIO AsyncIOProvider implementation

#ifdef _WIN32

#include "RIOAsyncIOProvider.h"
#include <cstring>
#include <vector>

namespace Network
{
namespace AsyncIO
{
namespace Windows
{

RIOAsyncIOProvider::RIOAsyncIOProvider()
	: mCompletionQueue(RIO_INVALID_CQ), mCompletionEvent(nullptr),
	  mMaxConcurrentOps(0), mNextBufferId(1), mInitialized(false)
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
	if (mInitialized)
	{
		mLastError = "Already initialized";
		return AsyncIOError::AlreadyInitialized;
	}

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
	mInitialized = true;

	return AsyncIOError::Success;
}

void RIOAsyncIOProvider::CleanupPendingOperation(
	std::unique_ptr<PendingOperation> &op)
{
	if (!op)
	{
		return;
	}

	if (op->mRioBufferId != RIO_INVALID_BUFFERID && mPfnRIODeregisterBuffer)
	{
		mPfnRIODeregisterBuffer(op->mRioBufferId);
		op->mRioBufferId = RIO_INVALID_BUFFERID;
	}

	op.reset();
}

void RIOAsyncIOProvider::Shutdown()
{
	if (!mInitialized)
	{
		return;
	}

	{
		std::lock_guard<std::mutex> lock(mMutex);

		for (auto &kv : mPendingOps)
		{
			CleanupPendingOperation(kv.second);
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

	mInitialized = false;
}

bool RIOAsyncIOProvider::IsInitialized() const
{
	return mInitialized;
}

AsyncIOError RIOAsyncIOProvider::GetOrCreateRequestQueue(
	SocketHandle socket, RIO_RQ &outQueue, RequestContext contextForSocket)
{
	if (!mInitialized)
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
	if (!mInitialized || !ptr || size == 0)
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
	if (!mInitialized)
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

	auto op = std::make_unique<PendingOperation>();
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
		mStats.mErrorCount++;
		return AsyncIOError::OperationFailed;
	}

	RIO_BUF rioBuffer;
	rioBuffer.BufferId = op->mRioBufferId;
	rioBuffer.Offset = 0;
	rioBuffer.Length = static_cast<ULONG>(size);

	void *opKey = op.get();
	{
		std::lock_guard<std::mutex> lock(mMutex);
		mPendingOps.emplace(opKey, std::move(op));
	}

	if (!mPfnRIOSend(requestQueue, &rioBuffer, 1, flags,
					 reinterpret_cast<void *>(opKey)))
	{
		std::unique_ptr<PendingOperation> failedOp;
		{
			std::lock_guard<std::mutex> lock(mMutex);
			auto it = mPendingOps.find(opKey);
			if (it != mPendingOps.end())
			{
				failedOp = std::move(it->second);
				mPendingOps.erase(it);
			}
			mStats.mErrorCount++;
		}
		CleanupPendingOperation(failedOp);
		mLastError = "RIOSend failed";
		return AsyncIOError::OperationFailed;
	}

	{
		std::lock_guard<std::mutex> lock(mMutex);
		mStats.mTotalRequests++;
		mStats.mPendingRequests++;
	}

	return AsyncIOError::Success;
}

AsyncIOError RIOAsyncIOProvider::RecvAsync(SocketHandle socket, void *buffer,
									  size_t size,
									  RequestContext context,
									  uint32_t flags)
{
	if (!mInitialized)
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

	auto op = std::make_unique<PendingOperation>();
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
		mStats.mErrorCount++;
		return AsyncIOError::OperationFailed;
	}

	RIO_BUF rioBuffer;
	rioBuffer.BufferId = op->mRioBufferId;
	rioBuffer.Offset = 0;
	rioBuffer.Length = static_cast<ULONG>(size);

	void *opKey = op.get();
	{
		std::lock_guard<std::mutex> lock(mMutex);
		mPendingOps.emplace(opKey, std::move(op));
	}

	if (!mPfnRIORecv(requestQueue, &rioBuffer, 1, flags,
					 reinterpret_cast<void *>(opKey)))
	{
		std::unique_ptr<PendingOperation> failedOp;
		{
			std::lock_guard<std::mutex> lock(mMutex);
			auto it = mPendingOps.find(opKey);
			if (it != mPendingOps.end())
			{
				failedOp = std::move(it->second);
				mPendingOps.erase(it);
			}
			mStats.mErrorCount++;
		}
		CleanupPendingOperation(failedOp);
		mLastError = "RIORecv failed";
		return AsyncIOError::OperationFailed;
	}

	{
		std::lock_guard<std::mutex> lock(mMutex);
		mStats.mTotalRequests++;
		mStats.mPendingRequests++;
	}

	return AsyncIOError::Success;
}

AsyncIOError RIOAsyncIOProvider::FlushRequests()
{
	if (!mInitialized)
	{
		return AsyncIOError::NotInitialized;
	}

	return AsyncIOError::Success;
}

int RIOAsyncIOProvider::ProcessCompletions(CompletionEntry *entries,
									 size_t maxEntries,
									 int timeoutMs)
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
	auto dequeue = [&]() -> ULONG {
		return mPfnRIODequeueCompletion(
			mCompletionQueue, rioResults.data(), static_cast<ULONG>(maxEntries));
	};

	ULONG numResults = dequeue();
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
		void *opKey = reinterpret_cast<void *>(
			static_cast<uintptr_t>(rioResults[i].RequestContext));
		std::unique_ptr<PendingOperation> op;
		{
			std::lock_guard<std::mutex> lock(mMutex);
			auto it = mPendingOps.find(opKey);
			if (it != mPendingOps.end())
			{
				op = std::move(it->second);
				mPendingOps.erase(it);
			}
		}

		if (!op)
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

		CleanupPendingOperation(op);

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
