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
// English: Lifecycle Management
// 한글: 생명주기 관리
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
// English: Socket Association
// 한글: 소켓 연결
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

	// English: Insert into pending map BEFORE calling WSASend, then release the lock.
	//          If WSASend posts an immediate completion to IOCP, a worker thread will
	//          dequeue it and try to acquire mMutex to look up the op in the map.
	//          Holding mMutex across WSASend would deadlock that worker thread.
	//          Save a copy of the WSABUF so we can call WSASend after releasing the lock.
	//
	//          Buffer lifetime safety: IOCP pins the send buffer (wsaBuf.buf → mBuffer)
	//          in kernel until the I/O completes. GQCS returns only after the kernel
	//          releases the buffer, so ProcessCompletions cannot destroy 'op' (and thus
	//          mBuffer) until after GQCS returns. Therefore wsaBuf.buf is valid for the
	//          entire duration of the WSASend call, even after the lock is released here.
	// 한글: WSASend 호출 전에 pending 맵에 먼저 삽입하고 락 해제.
	//       WSASend가 즉시 완료를 IOCP에 포스팅하면 워커 스레드가 mMutex를 획득해
	//       맵 조회를 시도함. WSASend 중 mMutex를 보유하면 해당 워커 스레드가 데드락.
	//       락 해제 후 WSASend 호출을 위해 WSABUF를 미리 복사.
	//
	//       버퍼 수명 안전성: IOCP는 I/O가 완료될 때까지 커널에서 전송 버퍼
	//       (wsaBuf.buf → mBuffer)를 pin함. GQCS는 커널이 버퍼를 해제한 후에만 반환
	//       하므로, ProcessCompletions는 GQCS 반환 후에야 'op'(즉 mBuffer)를 파괴할 수
	//       있음. 따라서 락 해제 후에도 WSASend 호출 기간 전체에서 wsaBuf.buf가 유효함.
	WSABUF wsaBuf;
	{
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

		wsaBuf = it->second->mWsaBuffer;
		mStats.mTotalRequests++;
		mStats.mPendingRequests++;
	}

	DWORD bytesSent = 0;
	int result = WSASend(socket, &wsaBuf, 1, &bytesSent, 0, opKey, nullptr);
	if (result == SOCKET_ERROR)
	{
		const int errorCode = WSAGetLastError();
		if (errorCode != WSA_IO_PENDING)
		{
			std::lock_guard<std::mutex> lock(mMutex);
			auto eraseIt = mPendingSendOps.find(opKey);
			if (eraseIt != mPendingSendOps.end())
			{
				mPendingSendOps.erase(eraseIt);
			}
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

	// English: Same lock-release pattern as SendAsync — insert into map first, then
	//          release mMutex before calling WSARecv to prevent deadlock with worker
	//          threads that process immediate IOCP completions under mMutex.
	// 한글: SendAsync와 동일한 락 해제 패턴 — 먼저 맵에 삽입 후 mMutex 해제,
	//       이후 WSARecv 호출로 즉시 완료를 처리하는 워커 스레드와의 데드락 방지.
	WSABUF wsaBuf;
	{
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

		wsaBuf = it->second->mWsaBuffer;
		mStats.mTotalRequests++;
		mStats.mPendingRequests++;
	}

	DWORD bytesRecv = 0;
	DWORD dwFlags = 0;
	int result = WSARecv(socket, &wsaBuf, 1, &bytesRecv, &dwFlags, opKey, nullptr);
	if (result == SOCKET_ERROR)
	{
		const int errorCode = WSAGetLastError();
		if (errorCode != WSA_IO_PENDING)
		{
			std::lock_guard<std::mutex> lock(mMutex);
			auto eraseIt = mPendingRecvOps.find(opKey);
			if (eraseIt != mPendingRecvOps.end())
			{
				mPendingRecvOps.erase(eraseIt);
			}
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
	if (!mInitialized.load(std::memory_order_acquire) ||
		mShuttingDown.load(std::memory_order_acquire))
	{
		// English: Return immediately during/after shutdown — mCompletionPort may be
		//          closed, and mPendingRecvOps/mPendingSendOps may have been cleared.
		//          Accessing pOverlapped from a dequeued completion after clear() is UB.
		// 한글: 종료 중/후에는 즉시 반환 — mCompletionPort가 닫혀 있을 수 있고,
		//       pending 맵이 이미 clear()됐을 수 있음.
		//       clear() 후 dequeue된 pOverlapped 접근은 UB.
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

		// English: Capture GetLastError() immediately — subsequent syscalls (mutex
		//          acquisition, map operations) may overwrite the TLS error value.
		// 한글: GetLastError()를 즉시 캡처 — 이후 syscall (mutex 획득, 맵 연산)이
		//       TLS 에러 값을 덮어쓸 수 있음.
		const DWORD capturedLastError = result ? 0 : ::GetLastError();

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
			static_cast<OSError>(capturedLastError);

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
