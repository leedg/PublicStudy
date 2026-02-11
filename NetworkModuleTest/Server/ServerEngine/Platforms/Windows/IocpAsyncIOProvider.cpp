// English: Windows IOCP AsyncIOProvider implementation
// 한글: Windows IOCP AsyncIOProvider 구현

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
// English: Constructor & Destructor
// 한글: 생성자 및 소멸자
// =============================================================================

IocpAsyncIOProvider::IocpAsyncIOProvider()
	: mCompletionPort(INVALID_HANDLE_VALUE), mMaxConcurrentOps(0),
	  mInitialized(false)
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
// English: Lifecycle Management
// 한글: 생명주기 관리
// =============================================================================

AsyncIOError IocpAsyncIOProvider::Initialize(size_t queueDepth,
											 size_t maxConcurrent)
{
	if (mInitialized)
	{
		mLastError = "Already initialized";
		return AsyncIOError::AlreadyInitialized;
	}

	// English: Create IOCP
	// 한글: IOCP 생성
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
	mInitialized = true;

	return AsyncIOError::Success;
}

void IocpAsyncIOProvider::Shutdown()
{
	if (!mInitialized)
	{
		return;
	}

	if (mCompletionPort && mCompletionPort != INVALID_HANDLE_VALUE)
	{
		CloseHandle(mCompletionPort);
		mCompletionPort = INVALID_HANDLE_VALUE;
	}

	std::lock_guard<std::mutex> lock(mMutex);
	mPendingRecvOps.clear();
	mPendingSendOps.clear();

	mInitialized = false;
}

bool IocpAsyncIOProvider::IsInitialized() const
{
	return mInitialized;
}

// =============================================================================
// English: Socket Association
// 한글: 소켓 연결
// =============================================================================

AsyncIOError IocpAsyncIOProvider::AssociateSocket(SocketHandle socket,
												  RequestContext context)
{
	if (!mInitialized)
	{
		mLastError = "Not initialized";
		return AsyncIOError::NotInitialized;
	}

	// English: Associate the socket with the IOCP completion port
	// 한글: 소켓을 IOCP 완료 포트에 연결
	// The completionKey (context) is typically the ConnectionId,
	// which will be returned in GetQueuedCompletionStatus completionKey parameter.
	// 완료 키(context)는 일반적으로 ConnectionId이며,
	// GetQueuedCompletionStatus의 completionKey 매개변수로 반환됩니다.
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
// English: Buffer Management
// 한글: 버퍼 관리
// =============================================================================

int64_t IocpAsyncIOProvider::RegisterBuffer(const void *ptr, size_t size)
{
	// English: IOCP doesn't need buffer registration
	// 한글: IOCP는 버퍼 등록 불필요
	return 0;
}

AsyncIOError IocpAsyncIOProvider::UnregisterBuffer(int64_t bufferId)
{
	// English: IOCP doesn't support buffer registration
	// 한글: IOCP는 버퍼 등록 미지원
	return AsyncIOError::Success;
}

// =============================================================================
// English: Async I/O Requests
// 한글: 비동기 I/O 요청
// =============================================================================

AsyncIOError IocpAsyncIOProvider::SendAsync(SocketHandle socket,
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

	// English: Create pending operation
	// 한글: 대기 작업 생성
	auto op = std::make_unique<PendingOperation>();
	std::memset(&op->mOverlapped, 0, sizeof(OVERLAPPED));

	op->mBuffer = std::make_unique<uint8_t[]>(size);
	std::memcpy(op->mBuffer.get(), buffer, size);

	op->mWsaBuffer.buf = reinterpret_cast<char*>(op->mBuffer.get());
	op->mWsaBuffer.len = static_cast<ULONG>(size);
	op->mContext = context;
	op->mType = AsyncIOType::Send;
	op->mSocket = socket;

	// English: Issue WSASend
	// 한글: WSASend 발행
	DWORD bytesSent = 0;
	int result = WSASend(socket, &op->mWsaBuffer, 1, &bytesSent, 0,
						 &op->mOverlapped, nullptr);

	if (result == SOCKET_ERROR)
	{
		int errorCode = WSAGetLastError();
		if (errorCode != WSA_IO_PENDING)
		{
			mLastError = "WSASend failed: " + std::to_string(errorCode);
			mStats.mErrorCount++;
			return AsyncIOError::OperationFailed;
		}
	}

	{
		std::lock_guard<std::mutex> lock(mMutex);
		mPendingSendOps[socket] = std::move(op);
		mStats.mTotalRequests++;
		mStats.mPendingRequests++;
	}

	return AsyncIOError::Success;
}

AsyncIOError IocpAsyncIOProvider::RecvAsync(SocketHandle socket, void *buffer,
											size_t size, RequestContext context,
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

	auto op = std::make_unique<PendingOperation>();
	std::memset(&op->mOverlapped, 0, sizeof(OVERLAPPED));

	op->mBuffer = std::make_unique<uint8_t[]>(size);
	op->mWsaBuffer.buf = reinterpret_cast<char*>(op->mBuffer.get());
	op->mWsaBuffer.len = static_cast<ULONG>(size);
	op->mContext = context;
	op->mType = AsyncIOType::Recv;
	op->mSocket = socket;

	DWORD bytesRecv = 0;
	DWORD dwFlags = 0;
	int result = WSARecv(socket, &op->mWsaBuffer, 1, &bytesRecv, &dwFlags,
						 &op->mOverlapped, nullptr);

	if (result == SOCKET_ERROR)
	{
		int errorCode = WSAGetLastError();
		if (errorCode != WSA_IO_PENDING)
		{
			mLastError = "WSARecv failed: " + std::to_string(errorCode);
			mStats.mErrorCount++;
			return AsyncIOError::OperationFailed;
		}
	}

	{
		std::lock_guard<std::mutex> lock(mMutex);
		mPendingRecvOps[socket] = std::move(op);
		mStats.mTotalRequests++;
		mStats.mPendingRequests++;
	}

	return AsyncIOError::Success;
}

AsyncIOError IocpAsyncIOProvider::FlushRequests()
{
	// English: IOCP executes immediately, no batching
	// 한글: IOCP는 즉시 실행, 배치 없음
	return AsyncIOError::Success;
}

// =============================================================================
// English: Completion Processing
// 한글: 완료 처리
// =============================================================================

int IocpAsyncIOProvider::ProcessCompletions(CompletionEntry *entries,
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

		// English: pOverlapped points to PendingOperation::mOverlapped (first member).
		//          Cast to PendingOperation* and use mSocket for O(1) map lookup.
		// 한글: pOverlapped는 PendingOperation::mOverlapped(첫 번째 멤버)를 가리킴.
		//       PendingOperation*로 캐스트 후 mSocket으로 O(1) 맵 탐색.
		std::unique_ptr<PendingOperation> op;
		if (pOverlapped)
		{
			// English: Cast OVERLAPPED* to PendingOperation* (mOverlapped is first member)
			// 한글: mOverlapped가 첫 번째 멤버이므로 OVERLAPPED* -> PendingOperation* 캐스트
			auto *candidate = reinterpret_cast<PendingOperation *>(pOverlapped);

			std::lock_guard<std::mutex> lock(mMutex);
			// English: O(1) lookup using mSocket key, verify pointer to guard against stale ops
			// 한글: mSocket 키로 O(1) 탐색, 포인터 확인으로 오래된 작업 방지
			auto recvIt = mPendingRecvOps.find(candidate->mSocket);
			if (recvIt != mPendingRecvOps.end() && recvIt->second.get() == candidate)
			{
				op = std::move(recvIt->second);
				mPendingRecvOps.erase(recvIt);
				mStats.mPendingRequests--;
			}
			else
			{
				auto sendIt = mPendingSendOps.find(candidate->mSocket);
				if (sendIt != mPendingSendOps.end() && sendIt->second.get() == candidate)
				{
					op = std::move(sendIt->second);
					mPendingSendOps.erase(sendIt);
					mStats.mPendingRequests--;
				}
			}
		}

		if (op)
		{
			// English: Matched a PendingOperation from Provider's RecvAsync/SendAsync
			// 한글: Provider의 RecvAsync/SendAsync에서 생성된 PendingOperation 매칭됨
			entries[completionCount].mContext = op->mContext;
			entries[completionCount].mType = op->mType;
		}
		else if (pOverlapped && completionKey != 0)
		{
			// English: No matching PendingOperation - this completion came from
			// Session::PostRecv/PostSend which directly calls WSARecv/WSASend
			// using Session's own IOContext (inherits OVERLAPPED).
			// IOContext has OVERLAPPED as its base class, so the cast is valid.
			// Use completionKey (= ConnectionId) as context.
			//
			// 한글: 매칭되는 PendingOperation 없음 - Session::PostRecv/PostSend가
			// 직접 WSARecv/WSASend를 호출하여 Session 자체의 IOContext
			// (OVERLAPPED를 상속)를 사용한 완료입니다.
			// IOContext는 OVERLAPPED를 기반 클래스로 가지므로 캐스트가 유효합니다.
			// completionKey(= ConnectionId)를 context로 사용합니다.
			auto *ioCtx = reinterpret_cast<Network::Core::IOContext *>(pOverlapped);

			entries[completionCount].mContext = static_cast<RequestContext>(completionKey);
			entries[completionCount].mType =
				(ioCtx->type == Network::Core::IOType::Recv)
					? AsyncIOType::Recv
					: AsyncIOType::Send;
		}
		else
		{
			// English: Unknown completion - skip
			// 한글: 알 수 없는 완료 - 건너뜀
			timeout = 0;
			continue;
		}

		entries[completionCount].mResult = result ? static_cast<int32_t>(bytesTransferred) : -1;
		entries[completionCount].mOsError = static_cast<OSError>(result ? 0 : ::GetLastError());

		auto endTime = std::chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime);
		entries[completionCount].mCompletionTime = duration.count();

		mStats.mTotalCompletions++;
		completionCount++;

		timeout = 0;
	}

	return completionCount;
}

// =============================================================================
// English: Information & Statistics
// 한글: 정보 및 통계
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
// English: Factory function
// 한글: 팩토리 함수
// =============================================================================

std::unique_ptr<AsyncIOProvider> CreateIocpProvider()
{
	return std::make_unique<IocpAsyncIOProvider>();
}

} // namespace Windows
} // namespace AsyncIO
} // namespace Network

#endif // _WIN32
