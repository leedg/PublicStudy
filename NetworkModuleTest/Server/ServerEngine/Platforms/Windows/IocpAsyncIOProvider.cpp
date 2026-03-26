// Windows IOCP AsyncIOProvider 구현

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
// 생성자 / 소멸자
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
// 생명주기 관리
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

	// IOCP 완료 포트 생성 (concurrentThreads=0 이면 OS가 자동 결정)
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
// 소켓 연결
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

	// 소켓을 IOCP 완료 포트에 연결한다.
	// completionKey(= context)는 ConnectionId이며, GQCS의 completionKey 매개변수로 반환된다.
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
// 버퍼 관리
// =============================================================================

int64_t IocpAsyncIOProvider::RegisterBuffer(const void *ptr, size_t size)
{
	// IOCP는 버퍼 사전 등록이 필요 없다 (per-op 동적 할당 방식).
	return 0;
}

AsyncIOError IocpAsyncIOProvider::UnregisterBuffer(int64_t bufferId)
{
	// UnregisterBuffer: IOCP는 버퍼 등록을 사용하지 않으므로 성공 반환.
	return AsyncIOError::Success;
}

// =============================================================================
// 비동기 I/O 요청
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

	// WSASend 호출 전에 pending 맵에 먼저 삽입하고 락을 해제한다.
	// 리즈니: WSASend가 즉시 완료를 IOCP에 포스팅하면 워커가 mMutex를 획득하려 데드락이 발생한다.
	// 버퍼 수명: IOCP는 I/O 완료까지 커널이 mBuffer를 pin하므로 GQCS 반환 전까지 wsaBuf.buf는 유효하다.
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
	// English: Use the caller-provided buffer directly as the WSARecv target.
	//          The caller must keep the buffer alive until the completion fires.
	//          No internal copy needed — received bytes land in caller's buffer.
	// 한글: 호출자가 제공한 버퍼를 WSARecv 수신 대상으로 직접 사용합니다.
	//       완료가 발생할 때까지 호출자가 버퍼를 유지해야 합니다.
	//       내부 복사 불필요 — 수신 데이터가 호출자 버퍼에 직접 기록됩니다.
	op->mBuffer = nullptr; // English: No internal buffer needed; caller owns the receive target.
	op->mWsaBuffer.buf = reinterpret_cast<char *>(buffer);
	op->mWsaBuffer.len = static_cast<ULONG>(size);
	op->mContext = context;
	op->mType = AsyncIOType::Recv;
	op->mSocket = socket;
	OVERLAPPED *opKey = &op->mOverlapped;

	// SendAsync와 동일한 락 해제 패턴: 먼저 맵에 삽입 후 mMutex 해제, 이후 WSARecv 호출.
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
	// IOCP는 배치 발송 개념이 없다. FlushRequests는 노옵으로 성공을 반환한다.
	return AsyncIOError::Success;
}

// =============================================================================
// 완료 처리
// =============================================================================

int IocpAsyncIOProvider::ProcessCompletions(CompletionEntry *entries,
											size_t maxEntries, int timeoutMs)
{
	if (!mInitialized.load(std::memory_order_acquire) ||
		mShuttingDown.load(std::memory_order_acquire))
	{
		// 종료 중/후에는 즉시 반환 — mCompletionPort가 닫혀 있거나
		// mPendingRecvOps/mPendingSendOps가 이미 clear()된 후
		// pOverlapped에 접근하면 UB가 발생한다.
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

		// GetLastError()를 즉시 캡처한다 — 이후 syscall(mutex 획득, 맵 연산)이
		// TLS 에러 값을 덮어쓸 수 있다.
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
// 정보 및 통계
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
// 팩토리 함수
// =============================================================================

std::unique_ptr<AsyncIOProvider> CreateIocpProvider()
{
	return std::make_unique<IocpAsyncIOProvider>();
}

} // namespace Windows
} // namespace AsyncIO
} // namespace Network

#endif // _WIN32
