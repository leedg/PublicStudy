#pragma once

// 클라이언트 세션 클래스 — 연결 관리

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
// 세션 상태
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
// IO 작업 타입
// =============================================================================

enum class IOType : uint8_t
{
	Accept,
	Recv,
	Send,
	Disconnect,
};

// =============================================================================
// IOCP overlapped 컨텍스트 (Windows 전용)
// =============================================================================

#ifdef _WIN32

struct IOContext : public OVERLAPPED
{
	IOType type;                    // 이 OVERLAPPED가 Recv/Send 중 어느 방향인지 식별
	WSABUF wsaBuf;                  // WSARecv/WSASend에 전달할 버퍼 기술자
	char buffer[RECV_BUFFER_SIZE];  // 수신 경로용 내장 버퍼 — Send 경로는 SendBufferPool 슬롯을 직접 지시

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
// 세션 클래스
// =============================================================================

class Session : public std::enable_shared_from_this<Session>
{
	// BaseNetworkEngine이 IOCP 완료 처리에서 PostSend()를 직접 호출하기 위해 접근 허용.
	// PostSend()는 Session 내부 상태(mSendContext, mCurrentSendSlotIdx)를 직접 다루므로
	// public 공개 대신 friend로 제한한다.
	friend class BaseNetworkEngine;

  public:
	Session();
	virtual ~Session();

	// 생명주기
	void Initialize(Utils::ConnectionId id, SocketHandle socket);
	void Close();

	// 모든 in-flight AsyncScope 태스크가 완료될 때까지 블로킹.
	// 풀 세션 반납 시 Close()와 Reset() 사이에 반드시 호출해야 한다.
	// Close()는 mAsyncScope.Cancel()을 호출(대기 태스크 건너뜀)하지만 블로킹하지 않는다.
	// 풀 세션은 ~Session()이 호출되지 않으므로 ~AsyncScope()의 RAII 드레인이
	// 실행되지 않는다. 이 메서드가 그 빈틈을 채운다.
	void WaitForPendingTasks();

	// 풀 재사용을 위한 세션 상태 초기화.
	// Close() + WaitForPendingTasks() 이후, 재Initialize() 이전에 호출.
	// ID·상태·카운터·recv 누적 버퍼 초기화.
	// mAsyncProvider는 Close()에서 정리. mRecvAccumBuffer는 여기서 초기화
	// (Close()에서 하지 않음) — race 이유는 Close() 주석 참고.
	void Reset();

	// 전송 결과 — 호출자에게 백프레셔 피드백을 제공하는 Send() 반환값.
	enum class SendResult : uint8_t
	{
		Ok,              // 패킷 큐잉/전송 성공
		QueueFull,       // 송신 큐 백프레셔 임계값 초과
		NotConnected,    // 세션 미연결
		InvalidArgument, // 과도한 크기이거나 null 패킷 — 재시도 불필요
	};

	// 패킷 전송. 백프레셔 피드백을 위해 SendResult 반환.
	SendResult Send(const void *data, uint32_t size);

	template <typename T> SendResult Send(const T &packet)
	{
		return Send(&packet, sizeof(T));
	}

	// IOCP에 수신 요청 등록 (POSIX에서는 AsyncIOProvider::RecvAsync()가 직접 구동하므로 미사용)
	bool PostRecv();

	// 접근자
	Utils::ConnectionId GetId() const { return mId; }
	SocketHandle GetSocket() const { return mSocket.load(std::memory_order_acquire); }
	SessionState GetState() const { return mState.load(std::memory_order_acquire); }
	bool IsConnected() const { return mState.load(std::memory_order_acquire) == SessionState::Connected; }

	Utils::Timestamp GetConnectTime() const { return mConnectTime; }
	Utils::Timestamp GetLastPingTime() const { return mLastPingTime; }
	void SetLastPingTime(Utils::Timestamp time) { mLastPingTime = time; }

	// 핑 시퀀스 — 핑 타이머 스레드와 I/O 워커 스레드가 동시에 접근할 수 있으므로 atomic 사용.
	// relaxed 순서로 충분: 시퀀스 번호 자체의 단조 증가만 보장되면 되며, 다른 데이터와의
	// happens-before 관계가 필요하지 않다.
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
	// 크로스 플랫폼 수신 버퍼 접근자
	char *GetRecvBuffer();
	const char *GetRecvBuffer() const;
	size_t GetRecvBufferSize() const;

	// 수신/송신 버퍼 접근 (IOCP 완료 처리용)
#ifdef _WIN32
	IOContext &GetRecvContext() { return mRecvContext; }
	IOContext &GetSendContext() { return mSendContext; }

	// OVERLAPPED 포인터 역참조 없이 IO 타입 조회.
	// IOCP 완료 경로에서 완료 패킷 처리 시 이미 해제됐을 수 있는 메모리를
	// 역참조하지 않고도 Recv/Send 방향을 판별하기 위해 SessionPool의 불변 맵을 활용.
	static bool TryResolveIOType(const OVERLAPPED *overlapped, IOType &outType);
#endif

	// 가상 이벤트 핸들러 (파생 클래스에서 오버라이드)
	virtual void OnConnected() {}
	virtual void OnDisconnected() {}
	virtual void OnRecv(const char *data, uint32_t size)
	{
		if (mOnRecvCb) mOnRecvCb(this, data, size);
	}

	// 세션별 recv 콜백 — SessionManager::CreateSession에서 PostRecv() 이전에 1회 설정.
	// Reset()에서 초기화. Session*를 인자로 포함하여 핸들러가 클로저에 raw ptr를
	// 캡처하지 않고 session->Send()를 호출할 수 있도록 함.
	using OnRecvCallback = std::function<void(Session*, const char*, uint32_t)>;
	void SetOnRecv(OnRecvCallback cb);

	// TCP 스트림 재조립 — 엔진이 IOCP/epoll 완료 후 원시 수신 바이트를 넘기는 진입점.
	// 내부적으로 PacketHeader.size 기반 패킷 경계를 검출하고 OnRecv()를 완성 패킷 단위로 호출한다.
	void ProcessRawRecv(const char *data, uint32_t size);

  private:
	// 내부 전송 처리
	void FlushSendQueue();
	bool PostSend();
	SocketHandle GetInvalidSocket() const;

  private:
	Utils::ConnectionId       mId;      // 세션 고유 식별자 — KeyedDispatcher 라우팅 키
	std::atomic<SocketHandle> mSocket;  // 소켓 핸들 — Close()에서 exchange()로 원자적 교체
	std::atomic<SessionState> mState;   // 연결 상태 — IsConnected()·Close() 경쟁 방지를 위해 atomic

	// ─── 시간 추적 ───────────────────────────────────────────────────────────
	Utils::Timestamp      mConnectTime;    // 세션이 Connected 상태로 전환된 타임스탬프
	Utils::Timestamp      mLastPingTime;   // 마지막 핑 수신 시간 — 타임아웃 감지 기준
	std::atomic<uint32_t> mPingSequence;   // 핑 시퀀스 번호 — 타이머·I/O 워커 동시 접근으로 atomic

	// ─── IO 컨텍스트 ─────────────────────────────────────────────────────────
	// Windows는 WSARecv/WSASend에 OVERLAPPED를 직접 제공해야 하므로 IOContext 내장.
	// POSIX는 epoll/io_uring 이벤트 루프가 fd를 직접 관리하므로 단순 바이트 버퍼만 유지.
#ifdef _WIN32
	IOContext mRecvContext;  // WSARecv OVERLAPPED 컨텍스트 + 수신 버퍼
	IOContext mSendContext;  // WSASend OVERLAPPED 컨텍스트 — mCurrentSendSlotIdx 슬롯의 wsaBuf 지시
#else
	std::array<char, RECV_BUFFER_SIZE> mRecvBuffer{};  // POSIX 플랫폼용 수신 버퍼 (RecvAsync()에 직접 전달)
#endif

	// ─── 전송 큐 ─────────────────────────────────────────────────────────────
	// Lock 경합 최적화가 적용된 전송 큐.
	// IOCP 경로(Windows): 풀 슬롯을 참조하는 SendRequest 사용 (0 alloc).
	// 다른 플랫폼: vector<char> 사용 (기존과 동일).
#ifdef _WIN32
	struct SendRequest
	{
		size_t   slotIdx;  // SendBufferPool 슬롯 인덱스
		uint32_t size;     // 페이로드 바이트 수
	};
	std::queue<SendRequest> mSendQueue;         // 미전송 요청 대기 큐 (mSendMutex 보호)
	// 현재 WSASend에 제출 중인 슬롯 인덱스.
	// ~size_t(0) = 전송 중인 슬롯 없음. 비트 반전값으로 초기화하면 별도 bool 플래그 없이
	// 유효/무효를 구분할 수 있어 분기가 단순해진다.
	size_t mCurrentSendSlotIdx;                 // 커널에 제출 중인 SendBufferPool 슬롯 — ~0이면 없음
#else
	std::queue<std::vector<char>> mSendQueue;   // 미전송 버퍼 큐 (mSendMutex 보호)
#endif
	std::mutex          mSendMutex;             // mSendQueue·mAsyncProvider 보호
	std::atomic<bool>   mIsSending;             // CAS로 이중 PostSend 방지 — true이면 send 루프 진행 중

	// Fast-path 최적화 — 큐 크기 카운터 (lock-free 읽기).
	// mSendQueue.size()는 mSendMutex 없이는 읽을 수 없으므로 별도 atomic 카운터를 유지한다.
	// 큐가 비어있을 가능성이 높은 경우 mutex 획득을 건너뛸 수 있다.
	std::atomic<size_t> mSendQueueSize;         // mSendQueue와 동기화된 크기 카운터 (lock-free 읽기용)

	// 비동기 I/O 공급자 — mSendMutex 보호.
	// SetAsyncProvider(), Close(), Send() RIO 경로, PostSend() POSIX 경로가
	// 이 필드 읽기/쓰기 전에 mSendMutex를 획득한다.
	std::shared_ptr<AsyncIO::AsyncIOProvider> mAsyncProvider;  // 플랫폼별 I/O 공급자 (Close()에서 reset)

	// ─── 수신 재조립 버퍼 ────────────────────────────────────────────────────
	// TCP 재조립 누적 버퍼 + 읽기 오프셋.
	// KeyedDispatcher 친화도로 동일 세션의 ProcessRawRecv가 항상 같은 워커에서 직렬화되므로
	// mRecvMutex 없이 안전하게 접근 가능.
	std::vector<char> mRecvAccumBuffer;      // TCP 스트림 재조립용 누적 버퍼 — mutex 없이 접근 가능
	size_t            mRecvAccumOffset{0};   // O(1) 읽기 포인터 — 절반 초과 시 compact

	// ProcessRawRecv 일반 경로용 재사용 배치 버퍼.
	// Initialize()에서 예약하여 호출 간 할당 비용을 상각.
	// OnRecv 디스패치 전 지역 변수와 swap하여 재사용 capacity를 보존.
	std::vector<char> mRecvBatchBuf;         // 완성 패킷 배치 임시 버퍼 — capacity 재사용으로 alloc 상각

	// 애플리케이션 수준 recv 콜백. SessionManager::CreateSession에서
	// PostRecv() 이전에 1회 설정 (첫 recv 완료보다 happens-before 보장).
	// Reset()에서 초기화하여 스테일 캡처 없이 슬롯 재사용 가능.
	OnRecvCallback mOnRecvCb;                // 완성 패킷 단위 애플리케이션 콜백 — Reset()에서 초기화

	// ─── 비동기 스코프 ───────────────────────────────────────────────────────
	// 큐잉된 로직 작업의 협력 취소를 위한 비동기 스코프.
	// BaseNetworkEngine이 Submit(...)으로 태스크를 제출하며,
	// Close() 이후 Cancel()로 대기 태스크를 조용히 건너뜀.
	Network::Concurrency::AsyncScope mAsyncScope;  // Close() 후 대기 태스크 취소 — RAII로 드레인 보장
};

using SessionRef = std::shared_ptr<Session>;
using SessionWeakRef = std::weak_ptr<Session>;

} // namespace Network::Core
