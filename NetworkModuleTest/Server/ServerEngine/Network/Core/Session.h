#pragma once

// English: Client session class for connection management
// 한글: 클라이언트 세션 클래스 — 연결 관리

#include "../../Concurrency/AsyncScope.h"
#include "../../Utils/NetworkUtils.h"
#include "AsyncIOProvider.h"
#include "PacketDefine.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>
#include <array>

namespace Network::Core
{
// =============================================================================
// English: Session state
// 한글: 세션 상태
// =============================================================================

enum class SessionState : uint8_t
{
	None,
	Connecting,
	Connected,
	Disconnecting,
	Disconnected,
};

// =============================================================================
// English: IO operation type
// 한글: IO 작업 타입
// =============================================================================

enum class IOType : uint8_t
{
	Accept,
	Recv,
	Send,
	Disconnect,
};

// =============================================================================
// English: IOCP overlapped context (Windows only)
// 한글: IOCP overlapped 컨텍스트 (Windows 전용)
// =============================================================================

#ifdef _WIN32

struct IOContext : public OVERLAPPED
{
	IOType type;
	WSABUF wsaBuf;
	char buffer[RECV_BUFFER_SIZE];

	IOContext(IOType ioType) : type(ioType)
	{
		memset(static_cast<OVERLAPPED *>(this), 0, sizeof(OVERLAPPED));
		memset(buffer, 0, sizeof(buffer));
		wsaBuf.buf = buffer;
		wsaBuf.len = sizeof(buffer);
	}

	void Reset()
	{
		memset(static_cast<OVERLAPPED *>(this), 0, sizeof(OVERLAPPED));
	}
};

#endif // _WIN32

// =============================================================================
// English: Session class
// 한글: 세션 클래스
// =============================================================================

class Session : public std::enable_shared_from_this<Session>
{
	// English: NetworkEngine classes need access to PostSend for completion handling
	// 한글: NetworkEngine 클래스가 완료 처리를 위해 PostSend에 접근해야 함
	friend class BaseNetworkEngine;

  public:
	Session();
	virtual ~Session();

	// English: Lifecycle
	// 한글: 생명주기
	void Initialize(Utils::ConnectionId id, SocketHandle socket);
	void Close();

	// English: Reset session state for pool reuse. Call after Close() and before re-Initialize().
	//          Clears ID, state, counters. mAsyncProvider and buffers are already cleaned by Close().
	// 한글: 풀 재사용을 위한 세션 상태 초기화. Close() 이후, 재Initialize() 이전에 호출.
	//       ID·상태·카운터 초기화. mAsyncProvider·버퍼는 Close()에서 이미 정리됨.
	void Reset();

	// English: Send result — returned by Send() to give the caller backpressure feedback.
	// 한글: 전송 결과 — 호출자에게 백프레셔 피드백을 제공하는 Send() 반환값.
	enum class SendResult : uint8_t
	{
		Ok,           // English: Packet enqueued/sent successfully / 한글: 패킷 큐잉/전송 성공
		QueueFull,    // English: Send queue above backpressure threshold / 한글: 송신 큐 백프레셔 임계값 초과
		NotConnected, // English: Session not connected / 한글: 세션 미연결
	};

	// English: Send packet. Returns SendResult for backpressure feedback.
	// 한글: 패킷 전송. 백프레셔 피드백을 위해 SendResult 반환.
	SendResult Send(const void *data, uint32_t size);

	template <typename T> SendResult Send(const T &packet)
	{
		return Send(&packet, sizeof(T));
	}

	// English: Post receive request to IOCP
	// 한글: IOCP에 수신 요청 등록
	bool PostRecv();

	// English: Accessors
	// 한글: 접근자
	Utils::ConnectionId GetId() const { return mId; }
	SocketHandle GetSocket() const { return mSocket.load(std::memory_order_acquire); }
	SessionState GetState() const { return mState.load(std::memory_order_acquire); }
	bool IsConnected() const { return mState.load(std::memory_order_acquire) == SessionState::Connected; }

	Utils::Timestamp GetConnectTime() const { return mConnectTime; }
	Utils::Timestamp GetLastPingTime() const { return mLastPingTime; }
	void SetLastPingTime(Utils::Timestamp time) { mLastPingTime = time; }

	// English: Ping sequence — atomic to prevent race between ping timer thread and I/O thread
	// 한글: 핑 시퀀스 — 핑 타이머 스레드와 I/O 스레드 간 race 방지를 위해 atomic 사용
	uint32_t GetPingSequence() const
	{
		return mPingSequence.load(std::memory_order_relaxed);
	}
	void IncrementPingSequence()
	{
		mPingSequence.fetch_add(1, std::memory_order_relaxed);
	}

	void SetAsyncProvider(std::shared_ptr<AsyncIO::AsyncIOProvider> provider)
	{
		std::lock_guard<std::mutex> lock(mSendMutex);
		mAsyncProvider = std::move(provider);
	}
	// English: Cross-platform recv buffer access
	// 한글: 크로스 플랫폼 수신 버퍼 접근자
	char *GetRecvBuffer();
	const char *GetRecvBuffer() const;
	size_t GetRecvBufferSize() const;

	// English: Access recv buffer (for IOCP completion)
	// 한글: 수신 버퍼 접근 (IOCP 완료 처리용)
#ifdef _WIN32
	IOContext &GetRecvContext() { return mRecvContext; }
	IOContext &GetSendContext() { return mSendContext; }

	// English: Resolve IO type by OVERLAPPED pointer without dereferencing it.
	//          Used by IOCP completion path to avoid touching freed memory.
	// 한국어: OVERLAPPED 포인터 역참조 없이 IO 타입을 조회.
	//       IOCP 완료 경로에서 해제된 메모리 접근을 피하기 위해 사용.
	static bool TryResolveIOType(const OVERLAPPED *overlapped, IOType &outType);
#endif

	// English: Virtual event handlers (override in derived classes)
	// 한글: 가상 이벤트 핸들러 (파생 클래스에서 오버라이드)
	virtual void OnConnected() {}
	virtual void OnDisconnected() {}
	virtual void OnRecv(const char *data, uint32_t size)
	{
		if (mOnRecvCb) mOnRecvCb(this, data, size);
	}

	// English: Per-session recv callback — set once in SessionManager::CreateSession via
	//          SetSessionConfigurator, before PostRecv() is issued. Cleared in Reset().
	//          Signature includes Session* so the handler can call session->Send() without
	//          capturing a raw pointer in the closure.
	// 한글: 세션별 recv 콜백 — SessionManager::CreateSession에서 PostRecv() 이전에 1회 설정.
	//       Reset()에서 초기화. Session*를 인자로 포함하여 핸들러가 클로저에 raw ptr를
	//       캡처하지 않고 session->Send()를 호출할 수 있도록 함.
	using OnRecvCallback = std::function<void(Session*, const char*, uint32_t)>;
	void SetOnRecv(OnRecvCallback cb);

	// English: TCP stream reassembly - engine calls this with raw bytes
	// 한글: TCP 스트림 재조립 - 엔진이 원시 바이트로 이 메서드를 호출
	void ProcessRawRecv(const char *data, uint32_t size);

  private:
	// English: Internal send processing
	// 한글: 내부 전송 처리
	void FlushSendQueue();
	bool PostSend();
	SocketHandle GetInvalidSocket() const;

  private:
	Utils::ConnectionId mId;
	std::atomic<SocketHandle> mSocket;
	std::atomic<SessionState> mState;

	// English: Time tracking
	// 한글: 시간 추적
	Utils::Timestamp mConnectTime;
	Utils::Timestamp mLastPingTime;
	std::atomic<uint32_t> mPingSequence;

	// English: IO contexts (Windows IOCP)
	// 한글: IO 컨텍스트 (Windows IOCP)
#ifdef _WIN32
	IOContext mRecvContext;
	IOContext mSendContext;
#else
	// English: Recv buffer for POSIX platforms
	// 한글: POSIX 플랫폼용 수신 버퍼
	std::array<char, RECV_BUFFER_SIZE> mRecvBuffer{};
#endif

	// English: Send queue with lock contention optimization.
	//          IOCP path (Windows): uses SendRequest referencing a pool slot (0 alloc).
	//          Other platforms: uses vector<char> (unchanged).
	// 한글: Lock 경합 최적화가 적용된 전송 큐.
	//       IOCP 경로(Windows): 풀 슬롯을 참조하는 SendRequest 사용 (0 alloc).
	//       다른 플랫폼: vector<char> 사용 (기존과 동일).
#ifdef _WIN32
	struct SendRequest
	{
		size_t   slotIdx; // English: index into SendBufferPool / 한글: SendBufferPool 슬롯 인덱스
		uint32_t size;    // English: payload byte count / 한글: 페이로드 바이트 수
	};
	std::queue<SendRequest> mSendQueue;
	size_t mCurrentSendSlotIdx; // English: in-flight slot index (~0 = none) / 한글: 전송 중 슬롯 인덱스 (~0 = 없음)
#else
	std::queue<std::vector<char>> mSendQueue;
#endif
	std::mutex mSendMutex;
	std::atomic<bool> mIsSending;

	// English: Fast-path optimization - queue size counter (lock-free read)
	// 한글: Fast-path 최적화 — 큐 크기 카운터 (lock-free 읽기)
	// Purpose: Avoid mutex lock when queue is likely empty
	// 목적: 큐가 비어있을 가능성이 높을 때 mutex 잠금 회피
	std::atomic<size_t> mSendQueueSize;

	// English: Async I/O provider — protected by mSendMutex.
	//          SetAsyncProvider(), Close(), Send() RIO path, and PostSend() POSIX
	//          path all lock mSendMutex before reading/writing this field.
	//          Copy the shared_ptr under the lock, then use the snapshot outside
	//          the lock to avoid holding mSendMutex during actual I/O calls.
	// 한글: 비동기 I/O 공급자 — mSendMutex 보호.
	//       SetAsyncProvider(), Close(), Send() RIO 경로, PostSend() POSIX 경로가
	//       이 필드 읽기/쓰기 전에 mSendMutex를 획득한다.
	//       락 내에서 shared_ptr을 복사한 뒤, 락 해제 후 스냅샷을 사용하여
	//       실제 I/O 호출 중 mSendMutex를 보유하지 않도록 한다.
	std::shared_ptr<AsyncIO::AsyncIOProvider> mAsyncProvider;

	// English: TCP reassembly accumulation buffer + read offset.
	//
	//   mRecvMutex removed — serialization is now guaranteed by KeyedDispatcher affinity.
	//   Same sessionId always routes to the same worker thread, so ProcessRawRecv calls
	//   for a given session are inherently sequential (no concurrent workers).
	//
	//   mRecvAccumOffset — O(1) read pointer (position B pattern).
	//                      Instead of erasing (O(n) memmove) after every packet, we advance
	//                      an offset and compact only when the offset exceeds half the buffer.
	//
	// 한글: TCP 재조립 누적 버퍼 + 읽기 오프셋.
	//
	//   mRecvMutex 제거 — KeyedDispatcher 친화도로 직렬화 보장.
	//   동일 sessionId는 항상 동일 워커로 라우팅되므로 동일 세션의
	//   ProcessRawRecv 호출은 본질적으로 순차 실행 (동시 워커 없음).
	//
	//   mRecvAccumOffset — O(1) 읽기 포인터.
	//                      패킷마다 erase(O(n) memmove) 대신 오프셋만 전진하고,
	//                      오프셋이 버퍼 절반을 초과하면 compact.
	std::vector<char> mRecvAccumBuffer;
	size_t            mRecvAccumOffset{0};

	// English: Reusable batch buffer for ProcessRawRecv general path.
	//          Reserved in Initialize() to amortise allocations across calls.
	//          Protected by mRecvMutex. Swapped with a local variable before
	//          dispatching so OnRecv is called without holding mRecvMutex.
	// 한글: ProcessRawRecv 일반 경로용 재사용 배치 버퍼.
	//       Initialize()에서 예약하여 호출 간 할당 비용을 상각.
	//       mRecvMutex 보호. 디스패치 전 지역 변수와 swap하여
	//       OnRecv 호출 시 mRecvMutex를 보유하지 않도록 한다.
	std::vector<char> mRecvBatchBuf;

	// English: Application-level recv callback. Set once before PostRecv() in
	//          SessionManager::CreateSession (happens-before first recv completion).
	//          Cleared in Reset() so the slot can be reused without stale captures.
	// 한글: 애플리케이션 수준 recv 콜백. SessionManager::CreateSession에서
	//       PostRecv() 이전에 1회 설정 (첫 recv 완료보다 happens-before 보장).
	//       Reset()에서 초기화하여 스테일 캡처 없이 슬롯 재사용 가능.
	OnRecvCallback mOnRecvCb;

	// English: Async scope for cooperative cancellation of queued logic tasks.
	//          BaseNetworkEngine calls mAsyncScope.Submit(...) instead of Dispatch() directly,
	//          so that tasks queued after Close() are silently skipped.
	//          RAII dtor calls Cancel() + WaitForDrain() ensuring no tasks run after Session dtor.
	// 한글: 큐잉된 로직 작업의 협력 취소를 위한 비동기 스코프.
	//       BaseNetworkEngine이 Dispatch() 대신 mAsyncScope.Submit(...)을 호출하여
	//       Close() 이후 큐잉된 작업이 조용히 건너뜀.
	//       RAII 소멸자가 Cancel() + WaitForDrain()을 자동 호출하여
	//       Session 소멸 후 작업이 실행되지 않도록 보장.
	Network::Concurrency::AsyncScope mAsyncScope;
};

using SessionRef = std::shared_ptr<Session>;
using SessionWeakRef = std::weak_ptr<Session>;

} // namespace Network::Core
