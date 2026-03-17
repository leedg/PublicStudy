// English: epoll-based AsyncIOProvider implementation
// 한글: epoll 기반 AsyncIOProvider 구현

#ifdef __linux__

#include "EpollAsyncIOProvider.h"
#include "Utils/Logger.h"
#include "PlatformDetect.h"
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <unistd.h>
#include <errno.h>

namespace Network
{
namespace AsyncIO
{
namespace Linux
{
// =============================================================================
// English: Constructor & Destructor
// 한글: 생성자 및 소멸자
// =============================================================================

EpollAsyncIOProvider::EpollAsyncIOProvider()
	: mEpollFd(-1), mInfo{}, mStats{}, mMaxConcurrentOps(0), mInitialized(false)
{
}

EpollAsyncIOProvider::~EpollAsyncIOProvider()
{
	// English: Ensure resources are released
	// 한글: 리소스 해제 보장
	Shutdown();
}

// =============================================================================
// English: Lifecycle Management
// 한글: 생명주기 관리
// =============================================================================

AsyncIOError EpollAsyncIOProvider::Initialize(size_t queueDepth,
												  size_t maxConcurrent)
{
	if (mInitialized.load(std::memory_order_acquire))
		return AsyncIOError::AlreadyInitialized;

	// English: Create epoll file descriptor with close-on-exec
	// 한글: close-on-exec로 epoll 파일 디스크립터 생성
	mEpollFd = epoll_create1(EPOLL_CLOEXEC);
	if (mEpollFd < 0)
	{
		mLastError = "epoll_create1 failed";
		return AsyncIOError::OperationFailed;
	}

	mMaxConcurrentOps = maxConcurrent;

	// English: Initialize provider info
	// 한글: 공급자 정보 초기화
	mInfo.mPlatformType = PlatformType::Epoll;
	mInfo.mName = "epoll";
	mInfo.mMaxQueueDepth = queueDepth;
	mInfo.mMaxConcurrentReq = maxConcurrent;
	mInfo.mSupportsBufferReg = false;
	mInfo.mSupportsBatching = false;
	mInfo.mSupportsZeroCopy = false;

	mInitialized.store(true, std::memory_order_release);
	return AsyncIOError::Success;
}

void EpollAsyncIOProvider::Shutdown()
{
	// English: Atomically transition mInitialized from true → false.
	//          compare_exchange_strong prevents a TOCTOU race: a concurrent
	//          ProcessCompletions call that passes the mInitialized.load() check
	//          before this CAS would, in the old two-step pattern, proceed to call
	//          epoll_wait on a closed fd (EBADF / crash). By setting the flag to
	//          false in one atomic step, any ProcessCompletions call that loads false
	//          after the CAS returns early — and any call already past the check will
	//          at worst call epoll_wait on a fd that is still open (we close it under
	//          the mutex below), which is safe.
	// 한글: mInitialized를 true → false로 원자적 전환.
	//       compare_exchange_strong으로 TOCTOU 경쟁 방지:
	//       기존 두 단계 패턴(체크 후 락)에서는 ProcessCompletions가 mInitialized.load()
	//       체크를 통과한 뒤 Shutdown이 fd를 닫으면 epoll_wait(-1)이 EBADF/크래시 유발.
	//       CAS로 플래그를 원자적으로 false로 설정하면, 이후 ProcessCompletions 호출은
	//       false를 보고 즉시 반환. 이미 체크를 통과한 호출은 아직 열린 fd에서
	//       epoll_wait를 호출하므로 안전.
	bool expected = true;
	if (!mInitialized.compare_exchange_strong(expected, false,
	        std::memory_order_acq_rel, std::memory_order_acquire))
		return;

	std::lock_guard<std::mutex> lock(mMutex);

	// English: Close epoll file descriptor
	// 한글: epoll 파일 디스크립터 닫기
	if (mEpollFd >= 0)
	{
		close(mEpollFd);
		mEpollFd = -1;
	}

	mPendingRecvOps.clear();
	mPendingSendOps.clear();
}

bool EpollAsyncIOProvider::IsInitialized() const
{
	return mInitialized.load(std::memory_order_acquire);
}

// =============================================================================
// English: Socket Association
// 한글: 소켓 연결
// =============================================================================

AsyncIOError EpollAsyncIOProvider::AssociateSocket(SocketHandle socket,
												   RequestContext context)
{
	if (!mInitialized.load(std::memory_order_acquire))
		return AsyncIOError::NotInitialized;

	// English: Register socket with epoll for read + error events.
	//          EPOLLONESHOT ensures only one worker thread is woken per event,
	//          eliminating thundering herd when multiple workers share one epoll fd.
	//          The socket must be re-armed after each event via EPOLL_CTL_MOD.
	// 한글: 소켓을 epoll에 읽기 + 에러 이벤트로 등록.
	//       EPOLLONESHOT으로 이벤트당 워커 하나만 깨워 thundering herd 방지.
	//       이벤트 발생 후 EPOLL_CTL_MOD로 재등록 필요.
	struct epoll_event ev;
	ev.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP | EPOLLONESHOT;
	ev.data.fd = socket;

	if (epoll_ctl(mEpollFd, EPOLL_CTL_ADD, socket, &ev) < 0)
	{
		mLastError = "epoll_ctl EPOLL_CTL_ADD failed";
		Utils::Logger::Error("EpollAsyncIOProvider::AssociateSocket - epoll_ctl EPOLL_CTL_ADD failed: " + std::string(strerror(errno)));
		return AsyncIOError::OperationFailed;
	}

	return AsyncIOError::Success;
}

// =============================================================================
// English: Buffer Management
// 한글: 버퍼 관리
// =============================================================================

int64_t EpollAsyncIOProvider::RegisterBuffer(const void *ptr, size_t size)
{
	// English: epoll doesn't support pre-registered buffers (no-op)
	// 한글: epoll은 사전 등록 버퍼를 지원하지 않음 (no-op)
	return -1;
}

AsyncIOError EpollAsyncIOProvider::UnregisterBuffer(int64_t bufferId)
{
	// English: Not supported on epoll
	// 한글: epoll에서 지원하지 않음
	return AsyncIOError::PlatformNotSupported;
}

// =============================================================================
// English: Async I/O Operations
// 한글: 비동기 I/O 작업
// =============================================================================

AsyncIOError EpollAsyncIOProvider::SendAsync(SocketHandle socket,
											 const void *buffer, size_t size,
											 RequestContext context,
											 uint32_t flags)
{
	if (!mInitialized.load(std::memory_order_acquire))
		return AsyncIOError::NotInitialized;
	if (socket < 0 || !buffer || size == 0)
		return AsyncIOError::InvalidParameter;

	// English: Guard against size_t → uint32_t truncation. If size > UINT32_MAX the
	//          static_cast below wraps silently; the partial-send path then computes
	//          remaining = mBufferSize - sent which evaluates to ~4 GiB, causing a
	//          multi-GiB heap allocation and out-of-bounds memcpy.
	// 한글: size_t → uint32_t 절단 방어. size > UINT32_MAX면 아래 static_cast가
	//       묵시적으로 wrap; 부분 전송 경로에서 remaining = mBufferSize - sent가
	//       ~4GiB로 계산되어 수 GiB 힙 할당 및 범위 초과 memcpy 발생.
	if (size > static_cast<size_t>(std::numeric_limits<uint32_t>::max()))
	{
		mLastError = "SendAsync: buffer size exceeds uint32_t max (4 GiB)";
		return AsyncIOError::InvalidParameter;
	}

	std::lock_guard<std::mutex> lock(mMutex);

	// English: Reject if a send is already in-flight for this socket. Overwriting
	//          a pending partial-send would silently drop the unfinished data.
	// 한글: 동일 소켓에 이미 in-flight send가 있으면 거부.
	//       대기 중인 부분 전송을 덮어쓰면 미전송 데이터가 소실됨.
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

	// English: Re-arm socket with EPOLLOUT added (EPOLLONESHOT requires explicit re-arm).
	// 한글: EPOLLOUT 추가하여 소켓 재등록 (EPOLLONESHOT이므로 명시적 재등록 필요).
	struct epoll_event ev;
	ev.events = EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLRDHUP | EPOLLONESHOT;
	ev.data.fd = socket;
	if (epoll_ctl(mEpollFd, EPOLL_CTL_MOD, socket, &ev) < 0)
	{
		Utils::Logger::Error("EpollAsyncIOProvider::SendAsync - epoll_ctl EPOLL_CTL_MOD failed: " + std::string(strerror(errno)));
		mPendingSendOps.erase(socket);
		mStats.mPendingRequests--;
		return AsyncIOError::OperationFailed;
	}

	return AsyncIOError::Success;
}

AsyncIOError EpollAsyncIOProvider::RecvAsync(SocketHandle socket, void *buffer,
											 size_t size,
											 RequestContext context,
											 uint32_t flags)
{
	if (!mInitialized.load(std::memory_order_acquire))
		return AsyncIOError::NotInitialized;
	if (socket < 0 || !buffer || size == 0)
		return AsyncIOError::InvalidParameter;

	std::lock_guard<std::mutex> lock(mMutex);

	// English: Store pending operation (caller manages buffer)
	// 한글: 대기 작업 저장 (호출자가 버퍼 관리)
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

	// English: Re-arm socket for EPOLLIN (EPOLLONESHOT disarms it after each event fires).
	//          Called on initial setup and after each completed recv, so EPOLL_CTL_MOD
	//          is always correct: if already armed, this refreshes the interest; if
	//          disarmed (fired), this re-enables it. If data arrived between AssociateSocket
	//          and here, the kernel will fire again immediately once re-armed.
	// 한글: EPOLLIN을 위해 소켓 재등록 (EPOLLONESHOT이 이벤트 발생 후 소켓을 비활성화함).
	//       초기 설정 및 recv 완료 후 항상 호출되므로 EPOLL_CTL_MOD가 항상 올바름.
	{
		struct epoll_event ev;
		ev.events  = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP | EPOLLONESHOT;
		ev.data.fd = socket;
		if (epoll_ctl(mEpollFd, EPOLL_CTL_MOD, socket, &ev) < 0)
		{
			Utils::Logger::Error(
				"EpollAsyncIOProvider::RecvAsync - epoll_ctl EPOLL_CTL_MOD failed: " +
				std::string(strerror(errno)));
			mPendingRecvOps.erase(socket);
			mStats.mTotalRequests--;
			mStats.mPendingRequests--;
			return AsyncIOError::OperationFailed;
		}
	}

	return AsyncIOError::Success;
}

AsyncIOError EpollAsyncIOProvider::FlushRequests()
{
	// English: epoll doesn't support batch processing (no-op)
	// 한글: epoll은 배치 처리를 지원하지 않음 (no-op)
	if (!mInitialized.load(std::memory_order_acquire))
		return AsyncIOError::NotInitialized;

	return AsyncIOError::Success;
}

// =============================================================================
// English: Completion Processing
// 한글: 완료 처리
// =============================================================================

int EpollAsyncIOProvider::ProcessCompletions(CompletionEntry *entries,
											 size_t maxEntries, int timeoutMs)
{
	if (!mInitialized.load(std::memory_order_acquire))
		return static_cast<int>(AsyncIOError::NotInitialized);
	if (!entries || maxEntries == 0 || mEpollFd < 0)
		return static_cast<int>(AsyncIOError::InvalidParameter);

	// English: Poll for events
	// 한글: 이벤트 폴링
	std::unique_ptr<struct epoll_event[]> events(
		new struct epoll_event[maxEntries]);
	int numEvents = epoll_wait(mEpollFd, events.get(),
								   static_cast<int>(maxEntries), timeoutMs);

	if (numEvents < 0)
	{
		// English: EINTR means a signal interrupted the wait — not a real error, retry.
		// 한글: EINTR은 시그널에 의한 중단 — 실제 에러가 아니므로 0을 반환하여 재시도.
		if (errno == EINTR)
			return 0;
		mLastError = "epoll_wait failed: " + std::string(strerror(errno));
		return static_cast<int>(AsyncIOError::OperationFailed);
	}

	if (numEvents == 0)
		return 0;

	int processedCount = 0;

	for (int i = 0; i < numEvents; ++i)
	{
		SocketHandle socket = events[i].data.fd;
		uint32_t evFlags = events[i].events;

		// English: If the entries array is full, re-arm this socket so it fires
		//          again in the next ProcessCompletions call. Preserve EPOLLOUT in
		//          the re-arm if the original event included it (a pending send op
		//          exists) so the send is not permanently lost.
		// 한글: entries 배열이 꽉 찬 경우, 소켓을 재등록하여 다음 ProcessCompletions
		//       호출 시 재발화하도록 한다. 원본 이벤트에 EPOLLOUT이 있으면 (pending
		//       send 존재) 재등록 시 포함하여 send가 영구 소실되지 않도록 한다.
		if (processedCount >= static_cast<int>(maxEntries))
		{
			uint32_t rearmEvents = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP | EPOLLONESHOT;
			if (evFlags & EPOLLOUT)
				rearmEvents |= EPOLLOUT;
			struct epoll_event rearmEv;
			rearmEv.events  = rearmEvents;
			rearmEv.data.fd = socket;
			epoll_ctl(mEpollFd, EPOLL_CTL_MOD, socket, &rearmEv);
			continue;
		}

		// English: Handle error / hangup events
		// 한글: 에러 / 연결 끊김 이벤트 처리
		if (evFlags & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
		{
			// English: Find any pending op context for this socket for disconnect reporting
			// 한글: 연결 해제 보고용 소켓의 대기 중인 작업 컨텍스트 검색
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
				entry.mOsError = (evFlags & EPOLLERR) ? EIO : 0;
				entry.mCompletionTime = 0;
				mStats.mTotalCompletions++;
				processedCount++;
			}
			continue;
		}

		// English: Handle EPOLLIN (readable)
		// 한글: EPOLLIN (읽기 가능) 처리
		if (evFlags & EPOLLIN)
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

			if (found)
			{
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
						// English: Re-insert into the map BEFORE calling epoll_ctl.
						//          If epoll_ctl runs first, another worker can receive the
						//          re-armed event before the map insertion completes and
						//          find no pending op — silently dropping the recv and
						//          hanging the session permanently.
						//          Pattern mirrors RecvAsync: map insertion → then re-arm.
						// 한글: epoll_ctl 호출 전 먼저 맵에 재삽입.
						//       epoll_ctl이 먼저 실행되면 다른 워커가 재등록된 이벤트를
						//       수신했을 때 맵에 op가 없어 recv를 조용히 버리고
						//       세션이 영구 hang됨. RecvAsync와 동일한 패턴.
						{
							std::lock_guard<std::mutex> lock(mMutex);
							mPendingRecvOps[socket] = std::move(pending);
							mStats.mPendingRequests++;
						}
						struct epoll_event rearmEv;
						rearmEv.events  = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP | EPOLLONESHOT;
						rearmEv.data.fd = socket;
						epoll_ctl(mEpollFd, EPOLL_CTL_MOD, socket, &rearmEv);
					}
					else
					{
						osError = errno;
						result = -1;
						CompletionEntry &entry = entries[processedCount];
						entry.mContext = pending.mContext;
						entry.mType = AsyncIOType::Recv;
						entry.mResult = result;
						entry.mOsError = osError;
						entry.mCompletionTime = 0;
						mStats.mTotalCompletions++;
						processedCount++;
					}
					continue;
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
		}

		// English: Handle EPOLLOUT (writable) - execute send then remove EPOLLOUT.
		//          NOTE: Do NOT gate this block on processedCount < maxEntries directly.
		//          When both EPOLLIN and EPOLLOUT fire in the same event, the EPOLLIN
		//          block above may have consumed the last available slot (processedCount
		//          now equals maxEntries). Skipping EPOLLOUT without re-arming would
		//          permanently lose the pending send because the top-of-loop re-arm
		//          already ran for this iteration (it saw the original evFlags before
		//          EPOLLIN was processed). Instead, check here and re-arm if full.
		// 한글: EPOLLOUT 처리 시 processedCount < maxEntries로 직접 가드하지 않음.
		//       EPOLLIN + EPOLLOUT이 동시에 발화할 때 EPOLLIN 블록이 마지막 슬롯을
		//       소모하면 (processedCount == maxEntries), 가드가 false가 되어 EPOLLOUT을
		//       건너뜀. 루프 상단 재등록은 이미 이 이터레이션 시작 시 수행됐으므로
		//       EPOLLOUT 재등록이 누락되어 pending send가 영구 소실됨. 여기서 체크하여
		//       꽉 찬 경우 재등록 처리.
		if (evFlags & EPOLLOUT)
		{
			if (processedCount >= static_cast<int>(maxEntries))
			{
				// English: entries[] full — re-arm with EPOLLOUT preserved so the
				//          pending send fires in the next ProcessCompletions call.
				// 한글: entries[] 꽉 참 — EPOLLOUT 보존하여 재등록, 다음
				//       ProcessCompletions 호출에서 send가 발화하도록 함.
				struct epoll_event rearmEv;
				rearmEv.events  = EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLRDHUP | EPOLLONESHOT;
				rearmEv.data.fd = socket;
				epoll_ctl(mEpollFd, EPOLL_CTL_MOD, socket, &rearmEv);
			}
			else
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

			if (found)
			{
				// English: Perform the actual send before deciding how to re-arm.
				//          MSG_NOSIGNAL suppresses SIGPIPE on Linux if the peer closed.
				//          A non-blocking socket may perform a partial send (returns
				//          0 < sent < mBufferSize); in that case re-queue the remainder
				//          and re-arm with EPOLLOUT — do NOT emit a completion entry so
				//          that the caller sees one atomic completion for the full write.
				// 한글: 재등록 방식을 결정하기 위해 실제 send를 먼저 수행.
				//       MSG_NOSIGNAL은 피어가 닫힌 경우 SIGPIPE를 억제함.
				//       논블로킹 소켓에서 부분 전송(0 < sent < mBufferSize) 가능;
				//       이 경우 나머지를 재큐하고 EPOLLOUT으로 재등록 — 호출자가
				//       전체 쓰기에 대한 원자적 완료를 볼 수 있도록 completion entry 미발행.
				int32_t result = 0;
				OSError osError = 0;
				ssize_t sent = ::send(socket, pending.mBuffer, pending.mBufferSize,
									  MSG_NOSIGNAL);

				if (sent >= 0 &&
					static_cast<uint32_t>(sent) < pending.mBufferSize)
				{
					// English: Partial send — re-queue the unsent tail and re-arm EPOLLOUT.
					// 한글: 부분 전송 — 미전송 나머지를 재큐하고 EPOLLOUT 재등록.
					const uint32_t remaining =
						pending.mBufferSize - static_cast<uint32_t>(sent);
					auto remainBuf = std::make_unique<uint8_t[]>(remaining);
					std::memcpy(remainBuf.get(),
								pending.mBuffer + sent,
								remaining);

					PendingOperation remainOp;
					remainOp.mContext     = pending.mContext;
					remainOp.mType        = AsyncIOType::Send;
					remainOp.mSocket      = socket;
					remainOp.mOwnedBuffer = std::move(remainBuf);
					remainOp.mBuffer      = remainOp.mOwnedBuffer.get();
					remainOp.mBufferSize  = remaining;

					{
						std::lock_guard<std::mutex> lock(mMutex);
						mPendingSendOps[socket] = std::move(remainOp);
						mStats.mPendingRequests++;
					}

					struct epoll_event ev;
					ev.events  = EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLRDHUP | EPOLLONESHOT;
					ev.data.fd = socket;
					epoll_ctl(mEpollFd, EPOLL_CTL_MOD, socket, &ev);
					continue; // no completion entry for partial send
				}

				// English: Full send or real error — re-arm for EPOLLIN only.
				// 한글: 전체 전송 또는 실제 에러 — EPOLLIN만으로 재등록.
				{
					struct epoll_event ev;
					ev.events  = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP | EPOLLONESHOT;
					ev.data.fd = socket;
					if (epoll_ctl(mEpollFd, EPOLL_CTL_MOD, socket, &ev) < 0)
					{
						Utils::Logger::Error(
							"EpollAsyncIOProvider::ProcessCompletions - "
							"epoll_ctl EPOLL_CTL_MOD (send done) failed: " +
							std::string(strerror(errno)));
					}
				}

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
			else
			{
				// English: No pending send op — re-arm without EPOLLOUT to avoid busy loop.
				// 한글: 대기 중인 송신 작업 없음 — busy loop 방지를 위해 EPOLLOUT 없이 재등록.
				struct epoll_event ev;
				ev.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP | EPOLLONESHOT;
				ev.data.fd = socket;
				epoll_ctl(mEpollFd, EPOLL_CTL_MOD, socket, &ev);
			}
		} // end else (processedCount < maxEntries)
		} // end if (evFlags & EPOLLOUT)
	}

	return processedCount;
}

// =============================================================================
// English: Information & Statistics
// 한글: 정보 및 통계
// =============================================================================

const ProviderInfo &EpollAsyncIOProvider::GetInfo() const { return mInfo; }

ProviderStats EpollAsyncIOProvider::GetStats() const { return mStats; }

const char *EpollAsyncIOProvider::GetLastError() const
{
	return mLastError.c_str();
}

// =============================================================================
// English: Factory Function
// 한글: 팩토리 함수
// =============================================================================

std::unique_ptr<AsyncIOProvider> CreateEpollProvider()
{
	return std::unique_ptr<AsyncIOProvider>(new EpollAsyncIOProvider());
}

} // namespace Linux
} // namespace AsyncIO
} // namespace Network

#endif // __linux__
