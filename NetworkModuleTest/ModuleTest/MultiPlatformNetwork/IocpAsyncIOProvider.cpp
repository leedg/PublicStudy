#ifdef _WIN32

#include "IocpAsyncIOProvider.h"
#include "../../Network/Core/PlatformDetect.h"
#include <chrono>

namespace Network
{
namespace AsyncIO
{
namespace Windows
{

IocpAsyncIOProvider::IocpAsyncIOProvider()
	: mCompletionPort(INVALID_HANDLE_VALUE), mMaxConcurrentOps(0),
	  mInitialized(false)
{
	// English: Initialize provider info
	// 한글: 공급자 정보 초기화
	std::memset(&mInfo, 0, sizeof(mInfo));
	std::memset(&mStats, 0, sizeof(mStats));

	mInfo.mPlatformType = PlatformType::IOCP;
	mInfo.mName = "IOCP";
	mInfo.mCapabilities = 0;
	mInfo.mSupportsBufferReg = false; // English: IOCP doesn't need buffer registration / 한글: IOCP는 버퍼 등록 불필요
	mInfo.mSupportsBatching = false;  // English: IOCP executes immediately / 한글: IOCP는 즉시 실행
	mInfo.mSupportsZeroCopy = false;
}

IocpAsyncIOProvider::~IocpAsyncIOProvider() { Shutdown(); }

AsyncIOError IocpAsyncIOProvider::Initialize(size_t queueDepth,
											 size_t maxConcurrent)
{
	// English: Check if already initialized
	// 한글: 이미 초기화되었는지 확인
	if (mInitialized)
	{
		mLastError = "Already initialized";
		return AsyncIOError::AlreadyInitialized;
	}

	// English: Create IOCP with specified concurrent threads (0 = auto)
	// 한글: 지정된 동시 스레드 수로 IOCP 생성 (0 = 자동)
	mCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr,
											 static_cast<ULONG_PTR>(maxConcurrent), 0);
	if (!mCompletionPort || mCompletionPort == INVALID_HANDLE_VALUE)
	{
		DWORD error = GetLastError();
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

	// English: Close completion port
	// 한글: 완료 포트 닫기
	if (mCompletionPort && mCompletionPort != INVALID_HANDLE_VALUE)
	{
		CloseHandle(mCompletionPort);
		mCompletionPort = INVALID_HANDLE_VALUE;
	}

	// English: Clean up pending operations
	// 한글: 대기 중인 작업 정리
	std::lock_guard<std::mutex> lock(mMutex);
	mPendingOps.clear();

	mInitialized = false;
}

bool IocpAsyncIOProvider::IsInitialized() const { return mInitialized; }

int64_t IocpAsyncIOProvider::RegisterBuffer(const void *ptr, size_t size)
{
	// English: IOCP doesn't require buffer registration (no-op)
	// 한글: IOCP는 버퍼 등록이 필요 없음 (no-op)
	return 0;
}

AsyncIOError IocpAsyncIOProvider::UnregisterBuffer(int64_t bufferId)
{
	// English: IOCP doesn't support buffer registration (no-op)
	// 한글: IOCP는 버퍼 등록을 지원하지 않음 (no-op)
	return AsyncIOError::Success;
}

AsyncIOError IocpAsyncIOProvider::SendAsync(SocketHandle socket,
											const void *buffer, size_t size,
											RequestContext context,
											uint32_t flags)
{
	if (!mInitialized)
	{
		mLastError = "Provider not initialized";
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

	// English: Allocate and copy buffer
	// 한글: 버퍼 할당 및 복사
	op->mBuffer = std::make_unique<uint8_t[]>(size);
	std::memcpy(op->mBuffer.get(), buffer, size);

	op->mWsaBuffer.buf = reinterpret_cast<char*>(op->mBuffer.get());
	op->mWsaBuffer.len = static_cast<ULONG>(size);
	op->mContext = context;
	op->mType = AsyncIOType::Send;

	// English: Issue WSASend
	// 한글: WSASend 발행
	DWORD bytesSent = 0;
	int result = WSASend(socket, &op->mWsaBuffer, 1, &bytesSent, 0,
						 &op->mOverlapped, nullptr);

	if (result == SOCKET_ERROR)
	{
		int error = WSAGetLastError();
		if (error != WSA_IO_PENDING)
		{
			mLastError = "WSASend failed: " + std::to_string(error);
			mStats.mErrorCount++;
			return AsyncIOError::OperationFailed;
		}
	}

	// English: Store pending operation
	// 한글: 대기 작업 저장
	{
		std::lock_guard<std::mutex> lock(mMutex);
		mPendingOps[socket] = std::move(op);
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
		mLastError = "Provider not initialized";
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
	op->mWsaBuffer.buf = reinterpret_cast<char*>(op->mBuffer.get());
	op->mWsaBuffer.len = static_cast<ULONG>(size);
	op->mContext = context;
	op->mType = AsyncIOType::Recv;

	// English: Issue WSARecv
	// 한글: WSARecv 발행
	DWORD bytesRecv = 0;
	DWORD dwFlags = 0;
	int result = WSARecv(socket, &op->mWsaBuffer, 1, &bytesRecv, &dwFlags,
						 &op->mOverlapped, nullptr);

	if (result == SOCKET_ERROR)
	{
		int error = WSAGetLastError();
		if (error != WSA_IO_PENDING)
		{
			mLastError = "WSARecv failed: " + std::to_string(error);
			mStats.mErrorCount++;
			return AsyncIOError::OperationFailed;
		}
	}

	// English: Store pending operation
	// 한글: 대기 작업 저장
	{
		std::lock_guard<std::mutex> lock(mMutex);
		mPendingOps[socket] = std::move(op);
		mStats.mTotalRequests++;
		mStats.mPendingRequests++;
	}

	return AsyncIOError::Success;
}

AsyncIOError IocpAsyncIOProvider::FlushRequests()
{
	// English: IOCP executes immediately (no batching), this is a no-op
	// 한글: IOCP는 즉시 실행됨 (배치 없음), no-op
	return AsyncIOError::Success;
}

int IocpAsyncIOProvider::ProcessCompletions(CompletionEntry *entries,
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

	// English: Convert timeout to milliseconds for IOCP
	// 한글: IOCP용 타임아웃을 밀리초로 변환
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

		// English: No more completions available
		// 한글: 더 이상 완료 항목 없음
		if (!result && !pOverlapped)
		{
			break;
		}

		// English: Find pending operation
		// 한글: 대기 작업 찾기
		std::unique_ptr<PendingOperation> op;
		{
			std::lock_guard<std::mutex> lock(mMutex);
			// English: Find by overlapped structure match
			// 한글: overlapped 구조체 일치로 찾기
			for (auto it = mPendingOps.begin(); it != mPendingOps.end(); ++it)
			{
				if (&it->second->mOverlapped == pOverlapped)
				{
					op = std::move(it->second);
					mPendingOps.erase(it);
					mStats.mPendingRequests--;
					break;
				}
			}
		}

		if (!op)
		{
			continue; // English: Unknown completion / 한글: 알 수 없는 완료
		}

		// English: Fill completion entry
		// 한글: 완료 항목 채우기
		entries[completionCount].mContext = op->mContext;
		entries[completionCount].mType = op->mType;
		entries[completionCount].mResult = result ? static_cast<int32_t>(bytesTransferred) : -1;
		entries[completionCount].mOsError = static_cast<OSError>(result ? 0 : GetLastError());

		auto endTime = std::chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime);
		entries[completionCount].mCompletionTime = duration.count();

		mStats.mTotalCompletions++;
		completionCount++;

		// English: After first completion, use non-blocking poll for remaining
		// 한글: 첫 번째 완료 후 나머지는 논블로킹 폴로 확인
		timeout = 0;
	}

	return completionCount;
}

const ProviderInfo &IocpAsyncIOProvider::GetInfo() const { return mInfo; }

ProviderStats IocpAsyncIOProvider::GetStats() const
{
	std::lock_guard<std::mutex> lock(mMutex);
	return mStats;
}

const char *IocpAsyncIOProvider::GetLastError() const
{
	return mLastError.c_str();
}

std::unique_ptr<AsyncIOProvider> CreateIocpProvider()
{
	return std::unique_ptr<AsyncIOProvider>(new IocpAsyncIOProvider());
}

} // namespace Windows
} // namespace AsyncIO
} // namespace Network

#endif
