// English: kqueue-based AsyncIOProvider implementation for macOS/BSD
// 한글: macOS/BSD용 kqueue 기반 AsyncIOProvider 구현

#ifdef __APPLE__

#include "KqueueAsyncIOProvider.h"
#include "PlatformDetect.h"
#include <cstring>
#include <limits>
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
	if (mInitialized.load(std::memory_order_acquire))
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

	mInitialized.store(true, std::memory_order_release);
	return AsyncIOError::Success;
}

void KqueueAsyncIOProvider::Shutdown()
{
	// English: Atomically transition mInitialized true → false (same rationale as
	//          EpollAsyncIOProvider::Shutdown — see comment there for full explanation).
	//          Without the CAS, a concurrent ProcessCompletions call that passes the
	//          mInitialized.load() check could call kevent() on a closed kqueue fd.
	// 한글: mInitialized를 true → false로 원자적 전환
	//       (EpollAsyncIOProvider::Shutdown과 동일한 이유 — 해당 주석 참고).
	//       CAS 없이는 ProcessCompletions가 mInitialized.load() 체크를 통과한 후
	//       닫힌 kqueue fd에서 kevent()를 호출할 수 있음.
	bool expected = true;
	if (!mInitialized.compare_exchange_strong(expected, false,
	        std::memory_order_acq_rel, std::memory_order_acquire))
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
}

bool KqueueAsyncIOProvider::IsInitialized() const
{
	return mInitialized.load(std::memory_order_acquire);
}

// =============================================================================
// English: Socket Association
// 한글: 소켓 연결
// =============================================================================

AsyncIOError KqueueAsyncIOProvider::AssociateSocket(SocketHandle socket,
													RequestContext context)
{
	if (!mInitialized.load(std::memory_order_acquire))
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
	if (!mInitialized.load(std::memory_order_acquire))
		return AsyncIOError::NotInitialized;
	if (socket < 0 || !buffer || size == 0)
		return AsyncIOError::InvalidParameter;

	// English: Guard against size_t → uint32_t truncation.
	//          Same rationale as EpollAsyncIOProvider::SendAsync — see comment there.
	// 한글: size_t → uint32_t 절단 방어.
	//       EpollAsyncIOProvider::SendAsync와 동일한 이유 — 해당 주석 참고.
	if (size > static_cast<size_t>(std::numeric_limits<uint32_t>::max()))
	{
		mLastError = "SendAsync: buffer size exceeds uint32_t max (4 GiB)";
		return AsyncIOError::InvalidParameter;
	}

	std::lock_guard<std::mutex> lock(mMutex);

	// English: Reject if a send is already in-flight for this socket.
	// 한글: 동일 소켓에 이미 in-flight send가 있으면 거부.
	if (mPendingSendOps.count(socket))
	{
		mLastError = "SendAsync: duplicate pending send for socket";
		return AsyncIOError::OperationFailed;
	}

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

	// English: Dynamically add EVFILT_WRITE with EV_ONESHOT so it fires exactly once,
	//          consistent with the partial-send re-arm path which also uses EV_ONESHOT.
	//          Without EV_ONESHOT, the initial registration is level-triggered while
	//          re-arms are one-shot, creating an inconsistency.
	// 한글: EV_ONESHOT으로 EVFILT_WRITE를 동적 추가 — 정확히 한 번만 발화.
	//       부분 전송 재등록 경로도 EV_ONESHOT을 사용하므로 일관성 유지.
	//       EV_ONESHOT 없이는 초기 등록이 레벨 트리거, 재등록은 원샷으로 불일치.
	struct kevent ev;
	EV_SET(&ev, socket, EVFILT_WRITE, EV_ADD | EV_ENABLE | EV_ONESHOT, 0, 0, nullptr);
	kevent(mKqueueFd, &ev, 1, nullptr, 0, nullptr);

	return AsyncIOError::Success;
}

AsyncIOError KqueueAsyncIOProvider::RecvAsync(SocketHandle socket, void *buffer,
												  size_t size,
												  RequestContext context,
												  uint32_t flags)
{
	if (!mInitialized.load(std::memory_order_acquire))
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

	// English: Re-add EVFILT_READ with EV_ONESHOT so only one worker is woken per event.
	//          EV_ONESHOT deletes the filter after it fires; we re-add it here each time
	//          RecvAsync is called (initial setup and after each completed recv).
	// 한글: 이벤트당 워커 하나만 깨우기 위해 EV_ONESHOT으로 EVFILT_READ 재등록.
	//       EV_ONESHOT은 발생 후 필터를 삭제하므로 RecvAsync 호출 시마다 재등록함.
	{
		struct kevent kev;
		EV_SET(&kev, socket, EVFILT_READ, EV_ADD | EV_ENABLE | EV_ONESHOT, 0, 0, nullptr);
		if (kevent(mKqueueFd, &kev, 1, nullptr, 0, nullptr) < 0)
		{
			Utils::Logger::Error(
				"KqueueAsyncIOProvider::RecvAsync - kevent EV_ADD | EV_ONESHOT failed: " +
				std::string(strerror(errno)));
			mPendingRecvOps.erase(socket);
			mStats.mTotalRequests--;
			mStats.mPendingRequests--;
			return AsyncIOError::OperationFailed;
		}
	}

	return AsyncIOError::Success;
}

AsyncIOError KqueueAsyncIOProvider::FlushRequests()
{
	// English: kqueue doesn't support batch processing (no-op)
	// 한글: kqueue는 배치 처리를 지원하지 않음 (no-op)
	if (!mInitialized.load(std::memory_order_acquire))
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
	if (!mInitialized.load(std::memory_order_acquire))
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

	if (numEvents < 0)
	{
		// English: EINTR means a signal interrupted the wait — not a real error, retry.
		//          All other negative returns are genuine errors (EBADF, EFAULT, etc.).
		// 한글: EINTR은 시그널에 의한 중단 — 실제 에러가 아니므로 0 반환하여 재시도.
		//       그 외 음수 반환은 실제 에러 (EBADF, EFAULT 등).
		if (errno == EINTR)
			return 0;
		mLastError = "kevent failed: " + std::string(strerror(errno));
		return static_cast<int>(AsyncIOError::OperationFailed);
	}
	if (numEvents == 0)
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
					// English: Re-insert into the map BEFORE calling kevent() to re-arm.
					//          EV_ONESHOT was consumed when EVFILT_READ fired — the filter
					//          is now deleted from the kqueue. Without re-arming here, the
					//          socket will never fire EVFILT_READ again and the recv hangs.
					//          Insert first so another worker cannot receive the re-armed
					//          event and find no pending op in the map.
					// 한글: kevent() 재등록 전 먼저 맵에 재삽입.
					//       EV_ONESHOT이 EVFILT_READ 발화 시 소모 — 필터가 kqueue에서 삭제됨.
					//       여기서 재등록하지 않으면 소켓이 다시 EVFILT_READ를 발화하지 않아
					//       recv가 영구 hang됨. 먼저 삽입하여 다른 워커가 재등록된 이벤트
					//       수신 시 맵에 op가 없는 상황을 방지.
					{
						std::lock_guard<std::mutex> lock(mMutex);
						mPendingRecvOps[socket] = std::move(pending);
						mStats.mPendingRequests++;
					}
					struct kevent rearmEv;
					EV_SET(&rearmEv, socket, EVFILT_READ,
					       EV_ADD | EV_ENABLE | EV_ONESHOT, 0, 0, nullptr);
					kevent(mKqueueFd, &rearmEv, 1, nullptr, 0, nullptr);
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
			// English: Perform send under the lock to eliminate the partial-send race
			// (same rationale as EpollAsyncIOProvider -- see comment there).
			// Non-blocking sockets return from send() immediately (bounded lock hold).
			bool found = false;
			bool partialSend = false;
			int32_t sendResult = 0;
			OSError sendError = 0;
			RequestContext sendCtx = 0;

			{
				std::lock_guard<std::mutex> lock(mMutex);
				auto it = mPendingSendOps.find(socket);
				if (it == mPendingSendOps.end())
				{
					// No pending send -- delete EVFILT_WRITE to avoid busy loop.
					struct kevent delev;
					EV_SET(&delev, socket, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
					kevent(mKqueueFd, &delev, 1, nullptr, 0, nullptr);
				}
				else
				{
					found = true;
					sendCtx = it->second.mContext;

					ssize_t sent = ::send(socket, it->second.mBuffer,
					                      it->second.mBufferSize, 0);

					if (sent >= 0 &&
					    static_cast<uint32_t>(sent) < it->second.mBufferSize)
					{
						// Partial send -- update in-place, keep in map.
						const uint32_t remaining =
							it->second.mBufferSize - static_cast<uint32_t>(sent);
						auto remainBuf = std::make_unique<uint8_t[]>(remaining);
						std::memcpy(remainBuf.get(), it->second.mBuffer + sent, remaining);
						it->second.mOwnedBuffer = std::move(remainBuf);
						it->second.mBuffer      = it->second.mOwnedBuffer.get();
						it->second.mBufferSize  = remaining;
						partialSend = true;
					}
					else
					{
						// Full send or error -- remove from map.
						if (sent >= 0)
							sendResult = static_cast<int32_t>(sent);
						else
						{
							sendError  = errno;
							sendResult = -1;
						}
						mPendingSendOps.erase(it);
						mStats.mPendingRequests--;
					}
				}
			}

			if (found)
			{
				if (partialSend)
				{
					struct kevent writeEv;
					EV_SET(&writeEv, socket, EVFILT_WRITE,
					       EV_ADD | EV_ENABLE | EV_ONESHOT, 0, 0, nullptr);
					kevent(mKqueueFd, &writeEv, 1, nullptr, 0, nullptr);
					// No completion entry for partial send
				}
				else
				{
					// Full send or error -- EV_ONESHOT already auto-removed; EV_DELETE is no-op.
					struct kevent delev;
					EV_SET(&delev, socket, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
					kevent(mKqueueFd, &delev, 1, nullptr, 0, nullptr);

					CompletionEntry &entry = entries[processedCount];
					entry.mContext = sendCtx;
					entry.mType = AsyncIOType::Send;
					entry.mResult = sendResult;
					entry.mOsError = sendError;
					entry.mCompletionTime = 0;
					mStats.mTotalCompletions++;
					processedCount++;
				}
			}
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
	// English: Register the socket with kqueue for error/EOF detection only.
	//          EVFILT_READ is added (with EV_ONESHOT) in RecvAsync each time a recv
	//          is queued, so it is not added here. EVFILT_WRITE is added dynamically
	//          in SendAsync. This avoids a race between AssociateSocket and RecvAsync
	//          where a spurious EV_ONESHOT fire could permanently silence EVFILT_READ.
	// 한글: 소켓을 에러/EOF 감지 전용으로 kqueue에 등록.
	//       EVFILT_READ는 RecvAsync에서 recv 큐잉 시마다 EV_ONESHOT으로 추가하므로
	//       여기서는 추가하지 않음. EVFILT_WRITE는 SendAsync에서 동적으로 추가.
	//       AssociateSocket과 RecvAsync 사이의 경쟁으로 EV_ONESHOT이 조기 발화하여
	//       EVFILT_READ가 영구적으로 침묵하는 경쟁 조건을 방지.
	(void)socket; // registration deferred to RecvAsync
	return true;
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
