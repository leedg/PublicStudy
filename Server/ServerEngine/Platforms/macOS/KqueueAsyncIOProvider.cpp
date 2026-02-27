// English: kqueue-based AsyncIOProvider implementation for macOS/BSD
// 한글: macOS/BSD용 kqueue 기반 AsyncIOProvider 구현

#ifdef __APPLE__

#include "KqueueAsyncIOProvider.h"
#include "PlatformDetect.h"
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>

namespace Network
{
namespace AsyncIO
{
namespace BSD
{
// =============================================================================
// English: Constructor & Destructor
// 한글: 생성자 및 소멸자
// =============================================================================

KqueueAsyncIOProvider::KqueueAsyncIOProvider()
	: mKqueueFd(-1), mInfo{}, mStats{}, mMaxConcurrentOps(0),
		  mInitialized(false)
{
}

KqueueAsyncIOProvider::~KqueueAsyncIOProvider() { Shutdown(); }

// =============================================================================
// English: Lifecycle Management
// 한글: 생명주기 관리
// =============================================================================

AsyncIOError KqueueAsyncIOProvider::Initialize(size_t queueDepth,
												   size_t maxConcurrent)
{
	if (mInitialized)
		return AsyncIOError::AlreadyInitialized;

	// English: Create kqueue file descriptor
	// 한글: kqueue 파일 디스크립터 생성
	mKqueueFd = kqueue();
	if (mKqueueFd < 0)
	{
		mLastError = "kqueue() failed";
		return AsyncIOError::OperationFailed;
	}

	mMaxConcurrentOps = maxConcurrent;

	// English: Initialize provider info
	// 한글: 공급자 정보 초기화
	mInfo.mPlatformType = PlatformType::Kqueue;
	mInfo.mName = "kqueue";
	mInfo.mMaxQueueDepth = queueDepth;
	mInfo.mMaxConcurrentReq = maxConcurrent;
	mInfo.mSupportsBufferReg = false;
	mInfo.mSupportsBatching = false;
	mInfo.mSupportsZeroCopy = false;

	mInitialized = true;
	return AsyncIOError::Success;
}

void KqueueAsyncIOProvider::Shutdown()
{
	if (!mInitialized)
		return;

	std::lock_guard<std::mutex> lock(mMutex);

	// English: Close kqueue file descriptor
	// 한글: kqueue 파일 디스크립터 닫기
	if (mKqueueFd >= 0)
	{
		close(mKqueueFd);
		mKqueueFd = -1;
	}

	mPendingRecvOps.clear();
	mPendingSendOps.clear();
	mRegisteredSockets.clear();
	mInitialized = false;
}

bool KqueueAsyncIOProvider::IsInitialized() const { return mInitialized; }

// =============================================================================
// English: Socket Association
// 한글: 소켓 연결
// =============================================================================

AsyncIOError KqueueAsyncIOProvider::AssociateSocket(SocketHandle socket,
													RequestContext context)
{
	if (!mInitialized)
		return AsyncIOError::NotInitialized;

	// English: Register socket with kqueue for read/write events
	// 한글: kqueue에 소켓을 읽기/쓰기 이벤트로 등록
	if (!RegisterSocketEvents(socket))
	{
		mLastError = "Failed to register socket events with kqueue";
		return AsyncIOError::OperationFailed;
	}

	std::lock_guard<std::mutex> lock(mMutex);
	mRegisteredSockets[socket] = true;

	return AsyncIOError::Success;
}

// =============================================================================
// English: Buffer Management
// 한글: 버퍼 관리
// =============================================================================

int64_t KqueueAsyncIOProvider::RegisterBuffer(const void *ptr, size_t size)
{
	// English: kqueue doesn't support pre-registered buffers (no-op)
	// 한글: kqueue는 사전 등록 버퍼를 지원하지 않음 (no-op)
	return -1;
}

AsyncIOError KqueueAsyncIOProvider::UnregisterBuffer(int64_t bufferId)
{
	return AsyncIOError::PlatformNotSupported;
}

// =============================================================================
// English: Async I/O Operations
// 한글: 비동기 I/O 작업
// =============================================================================

AsyncIOError KqueueAsyncIOProvider::SendAsync(SocketHandle socket,
												  const void *buffer, size_t size,
												  RequestContext context,
												  uint32_t flags)
{
	if (!mInitialized)
		return AsyncIOError::NotInitialized;
	if (socket < 0 || !buffer || size == 0)
		return AsyncIOError::InvalidParameter;

	std::lock_guard<std::mutex> lock(mMutex);

	// English: Store pending operation with buffer copy
	// 한글: 버퍼 복사와 함께 대기 작업 저장
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

	// English: Dynamically add EVFILT_WRITE so we get notified when socket is writable
	// 한글: 소켓이 쓰기 가능할 때 알림을 받기 위해 EVFILT_WRITE 동적 추가
	struct kevent ev;
	EV_SET(&ev, socket, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, nullptr);
	kevent(mKqueueFd, &ev, 1, nullptr, 0, nullptr);

	return AsyncIOError::Success;
}

AsyncIOError KqueueAsyncIOProvider::RecvAsync(SocketHandle socket, void *buffer,
												  size_t size,
												  RequestContext context,
												  uint32_t flags)
{
	if (!mInitialized)
		return AsyncIOError::NotInitialized;
	if (socket < 0 || !buffer || size == 0)
		return AsyncIOError::InvalidParameter;

	std::lock_guard<std::mutex> lock(mMutex);

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

AsyncIOError KqueueAsyncIOProvider::FlushRequests()
{
	// English: kqueue doesn't support batch processing (no-op)
	// 한글: kqueue는 배치 처리를 지원하지 않음 (no-op)
	if (!mInitialized)
		return AsyncIOError::NotInitialized;

	return AsyncIOError::Success;
}

// =============================================================================
// English: Completion Processing
// 한글: 완료 처리
// =============================================================================

int KqueueAsyncIOProvider::ProcessCompletions(CompletionEntry *entries,
												  size_t maxEntries, int timeoutMs)
{
	if (!mInitialized)
		return static_cast<int>(AsyncIOError::NotInitialized);
	if (!entries || maxEntries == 0 || mKqueueFd < 0)
		return static_cast<int>(AsyncIOError::InvalidParameter);

	// English: Prepare timeout structure
	// 한글: 타임아웃 구조체 준비
	struct timespec ts;
	struct timespec *pts = nullptr;

	if (timeoutMs >= 0)
	{
		ts.tv_sec = timeoutMs / 1000;
		ts.tv_nsec = (timeoutMs % 1000) * 1000000;
		pts = &ts;
	}

	// English: Poll for events
	// 한글: 이벤트 폴링
	std::unique_ptr<struct kevent[]> events(new struct kevent[maxEntries]);
	int numEvents = kevent(mKqueueFd, nullptr, 0, events.get(),
							   static_cast<int>(maxEntries), pts);

	if (numEvents <= 0)
		return 0;

	int processedCount = 0;

	for (int i = 0;
		 i < numEvents && processedCount < static_cast<int>(maxEntries); ++i)
	{
		struct kevent &event = events[i];
		SocketHandle socket = static_cast<SocketHandle>(event.ident);

		// English: Handle errors/EOF
		// 한글: 에러/EOF 처리
		if (event.flags & EV_ERROR)
		{
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
				entry.mOsError = static_cast<OSError>(event.data);
				entry.mCompletionTime = 0;
				mStats.mTotalCompletions++;
				processedCount++;
			}
			continue;
		}

		if (event.filter == EVFILT_READ)
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

			if (!found) continue;

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
					continue;
				}
				osError = errno;
				result = -1;
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
		else if (event.filter == EVFILT_WRITE)
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

			// English: Remove EVFILT_WRITE after consuming send op (or if none found)
			// 한글: 송신 작업 소비 후 (또는 없을 경우) EVFILT_WRITE 제거
			struct kevent delev;
			EV_SET(&delev, socket, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
			kevent(mKqueueFd, &delev, 1, nullptr, 0, nullptr);

			if (!found) continue;

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
	}

	return processedCount;
}

// =============================================================================
// English: Helper Methods
// 한글: 헬퍼 메서드
// =============================================================================

bool KqueueAsyncIOProvider::RegisterSocketEvents(SocketHandle socket)
{
	// English: Register for read events only; EVFILT_WRITE added dynamically on SendAsync
	// 한글: 읽기 이벤트만 등록; EVFILT_WRITE는 SendAsync 시 동적으로 추가
	struct kevent ev;
	EV_SET(&ev, socket, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);

	return kevent(mKqueueFd, &ev, 1, nullptr, 0, nullptr) >= 0;
}

bool KqueueAsyncIOProvider::UnregisterSocketEvents(SocketHandle socket)
{
	// English: Delete read and write events
	// 한글: 읽기 및 쓰기 이벤트 삭제
	struct kevent events[2];
	EV_SET(&events[0], socket, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
	EV_SET(&events[1], socket, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);

	// English: Ignore errors (socket might already be closed)
	// 한글: 에러 무시 (소켓이 이미 닫혔을 수 있음)
	kevent(mKqueueFd, events, 2, nullptr, 0, nullptr);
	return true;
}

// =============================================================================
// English: Information & Statistics
// 한글: 정보 및 통계
// =============================================================================

const ProviderInfo &KqueueAsyncIOProvider::GetInfo() const { return mInfo; }

ProviderStats KqueueAsyncIOProvider::GetStats() const { return mStats; }

const char *KqueueAsyncIOProvider::GetLastError() const
{
	return mLastError.c_str();
}

// =============================================================================
// English: Factory Function
// 한글: 팩토리 함수
// =============================================================================

std::unique_ptr<AsyncIOProvider> CreateKqueueProvider()
{
	return std::unique_ptr<AsyncIOProvider>(new KqueueAsyncIOProvider());
}

} // namespace BSD
} // namespace AsyncIO
} // namespace Network

#endif // __APPLE__
