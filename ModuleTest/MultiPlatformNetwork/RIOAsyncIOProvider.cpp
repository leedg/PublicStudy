#ifdef _WIN32

#include "RIOAsyncIOProvider.h"
#include "../../Network/Core/PlatformDetect.h"
#include <chrono>

namespace Network
{
namespace AsyncIO
{
namespace Windows
{

RIOAsyncIOProvider::RIOAsyncIOProvider()
	: mCompletionQueue(RIO_INVALID_CQ), mMaxConcurrentOps(0), mNextBufferId(1),
	  mInitialized(false)
{
	// English: Initialize provider info
	// 한글: 공급자 정보 초기화
	std::memset(&mInfo, 0, sizeof(mInfo));
	std::memset(&mStats, 0, sizeof(mStats));

	mInfo.mPlatformType = PlatformType::RIO;
	mInfo.mName = "RIO";
	mInfo.mCapabilities = 0;
	mInfo.mSupportsBufferReg = true;  // English: RIO requires buffer registration / 한글: RIO는 버퍼 등록 필요
	mInfo.mSupportsBatching = true;   // English: RIO supports deferred sends / 한글: RIO는 지연 전송 지원
	mInfo.mSupportsZeroCopy = true;

	// English: Initialize RIO function pointers to null
	// 한글: RIO 함수 포인터를 null로 초기화
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

RIOAsyncIOProvider::~RIOAsyncIOProvider() { Shutdown(); }

bool RIOAsyncIOProvider::LoadRIOFunctions()
{
	// English: Create temporary socket to get RIO extension function pointers
	// 한글: RIO 확장 함수 포인터를 얻기 위한 임시 소켓 생성
	SOCKET tempSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (tempSocket == INVALID_SOCKET)
	{
		mLastError = "Failed to create temporary socket";
		return false;
	}

	// English: Get RIO extension function table
	// 한글: RIO 확장 함수 테이블 가져오기
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

	// English: Extract function pointers from table
	// 한글: 테이블에서 함수 포인터 추출
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

AsyncIOError RIOAsyncIOProvider::Initialize(size_t queueDepth,
											size_t maxConcurrent)
{
	if (mInitialized)
	{
		mLastError = "Already initialized";
		return AsyncIOError::AlreadyInitialized;
	}

	// English: Load RIO function pointers
	// 한글: RIO 함수 포인터 로드
	if (!LoadRIOFunctions())
	{
		return AsyncIOError::PlatformNotSupported;
	}

	// English: Create RIO completion queue
	// 한글: RIO 완료 큐 생성
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

	// English: Deregister all buffers
	// 한글: 모든 버퍼 등록 해제
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
	}

	// English: Close all request queues
	// 한글: 모든 요청 큐 닫기
	{
		std::lock_guard<std::mutex> lock(mMutex);
		mRequestQueues.clear();
	}

	// English: Close completion queue
	// 한글: 완료 큐 닫기
	if (mCompletionQueue != RIO_INVALID_CQ && mPfnRIOCloseCompletionQueue)
	{
		mPfnRIOCloseCompletionQueue(mCompletionQueue);
		mCompletionQueue = RIO_INVALID_CQ;
	}

	mInitialized = false;
}

bool RIOAsyncIOProvider::IsInitialized() const { return mInitialized; }

int64_t RIOAsyncIOProvider::RegisterBuffer(const void *ptr, size_t size)
{
	if (!mInitialized)
	{
		return -1;
	}

	if (!ptr || size == 0)
	{
		return -1;
	}

	// English: Register buffer with RIO
	// 한글: RIO로 버퍼 등록
	RIO_BUFFERID rioBufferId = mPfnRIORegisterBuffer(
		const_cast<PCHAR>(static_cast<const char*>(ptr)),
		static_cast<DWORD>(size));

	if (rioBufferId == RIO_INVALID_BUFFERID)
	{
		mLastError = "Failed to register buffer";
		return -1;
	}

	// English: Store registered buffer info
	// 한글: 등록된 버퍼 정보 저장
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

	// English: Deregister buffer from RIO
	// 한글: RIO에서 버퍼 등록 해제
	if (mPfnRIODeregisterBuffer(it->second.mRioBufferId) != 0)
	{
		mLastError = "Failed to deregister buffer";
		return AsyncIOError::OperationFailed;
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
		mLastError = "Provider not initialized";
		return AsyncIOError::NotInitialized;
	}

	// English: Get or create request queue for this socket
	// 한글: 이 소켓의 요청 큐 가져오기 또는 생성
	RIO_RQ requestQueue;
	{
		std::lock_guard<std::mutex> lock(mMutex);
		auto it = mRequestQueues.find(socket);
		if (it == mRequestQueues.end())
		{
			// English: Create new request queue
			// 한글: 새 요청 큐 생성
			requestQueue = mPfnRIOCreateRequestQueue(
				socket,
				static_cast<DWORD>(mMaxConcurrentOps),
				static_cast<DWORD>(mMaxConcurrentOps),
				mCompletionQueue,
				mCompletionQueue,
				nullptr);

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

	// English: Create RIO buffer descriptor (assumes buffer is registered)
	// 한글: RIO 버퍼 디스크립터 생성 (버퍼가 등록되었다고 가정)
	RIO_BUF rioBuffer;
	rioBuffer.BufferId = RIO_INVALID_BUFFERID; // English: Use system buffer / 한글: 시스템 버퍼 사용
	rioBuffer.Offset = static_cast<ULONG>(reinterpret_cast<ULONG_PTR>(buffer));
	rioBuffer.Length = static_cast<ULONG>(size);

	// English: Issue RIO send
	// 한글: RIO 전송 발행
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
		mLastError = "Provider not initialized";
		return AsyncIOError::NotInitialized;
	}

	// English: Get or create request queue
	// 한글: 요청 큐 가져오기 또는 생성
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
				mCompletionQueue, mCompletionQueue, nullptr);

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

	// English: Create RIO buffer descriptor
	// 한글: RIO 버퍼 디스크립터 생성
	RIO_BUF rioBuffer;
	rioBuffer.BufferId = RIO_INVALID_BUFFERID;
	rioBuffer.Offset = static_cast<ULONG>(reinterpret_cast<ULONG_PTR>(buffer));
	rioBuffer.Length = static_cast<ULONG>(size);

	// English: Issue RIO receive
	// 한글: RIO 수신 발행
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

	// English: Notify RIO to commit all deferred sends
	// 한글: RIO에 모든 지연 전송 커밋 알림
	if (mCompletionQueue != RIO_INVALID_CQ && mPfnRIONotify)
	{
		mPfnRIONotify(mCompletionQueue);
	}

	return AsyncIOError::Success;
}

int RIOAsyncIOProvider::ProcessCompletions(CompletionEntry *entries,
										   size_t maxEntries, int timeoutMs)
{
	if (!mInitialized)
	{
		mLastError = "Provider not initialized";
		return static_cast<int>(AsyncIOError::NotInitialized);
	}

	if (!entries || maxEntries == 0)
	{
		mLastError = "Invalid parameters";
		return static_cast<int>(AsyncIOError::InvalidParameter);
	}

	// English: Allocate RIO result array
	// 한글: RIO 결과 배열 할당
	std::vector<RIORESULT> rioResults(maxEntries);

	// English: Dequeue completions from RIO
	// 한글: RIO에서 완료 항목 제거
	ULONG numResults = mPfnRIODequeueCompletion(
		mCompletionQueue, rioResults.data(), static_cast<ULONG>(maxEntries));

	if (numResults == RIO_CORRUPT_CQ)
	{
		mLastError = "RIO completion queue corrupted";
		mStats.mErrorCount++;
		return static_cast<int>(AsyncIOError::OperationFailed);
	}

	// English: Convert RIO results to CompletionEntry
	// 한글: RIO 결과를 CompletionEntry로 변환
	for (ULONG i = 0; i < numResults; ++i)
	{
		entries[i].mContext = static_cast<RequestContext>(rioResults[i].RequestContext);
		entries[i].mType = AsyncIOType::Send; // English: Determine from context / 한글: 컨텍스트에서 결정
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

const ProviderInfo &RIOAsyncIOProvider::GetInfo() const { return mInfo; }

ProviderStats RIOAsyncIOProvider::GetStats() const
{
	return mStats;
}

const char *RIOAsyncIOProvider::GetLastError() const
{
	return mLastError.c_str();
}

std::unique_ptr<AsyncIOProvider> CreateRIOProvider()
{
	return std::unique_ptr<AsyncIOProvider>(new RIOAsyncIOProvider());
}

} // namespace Windows
} // namespace AsyncIO
} // namespace Network

#endif
