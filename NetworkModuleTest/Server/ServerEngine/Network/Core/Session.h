#pragma once

// English: Client session class for connection management
// 한글: 연결 관리를 위한 클라이언트 세션 클래스

#include "../../Utils/NetworkUtils.h"
#include "AsyncIOProvider.h"
#include "PacketDefine.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>

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
// 한글: IOCP 오버랩 컨텍스트 (Windows 전용)
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
	// 한글: NetworkEngine 클래스들이 완료 처리를 위해 PostSend 접근 필요
	friend class BaseNetworkEngine;

  public:
	Session();
	virtual ~Session();

	// English: Lifecycle
	// 한글: 생명주기
	void Initialize(Utils::ConnectionId id, SocketHandle socket);
	void Close();

	// English: Send packet
	// 한글: 패킷 전송
	void Send(const void *data, uint32_t size);

	template <typename T> void Send(const T &packet)
	{
		Send(&packet, sizeof(T));
	}

	// English: Post receive request to IOCP
	// 한글: IOCP에 수신 요청 등록
	bool PostRecv();

	// English: Accessors
	// 한글: 접근자
	Utils::ConnectionId GetId() const { return mId; }
	SocketHandle GetSocket() const { return mSocket; }
	SessionState GetState() const { return mState; }
	bool IsConnected() const { return mState == SessionState::Connected; }

	Utils::Timestamp GetConnectTime() const { return mConnectTime; }
	Utils::Timestamp GetLastPingTime() const { return mLastPingTime; }
	void SetLastPingTime(Utils::Timestamp time) { mLastPingTime = time; }
	uint32_t GetPingSequence() const { return mPingSequence; }
	void IncrementPingSequence() { ++mPingSequence; }

	// English: Access recv buffer (for IOCP completion)
	// 한글: 수신 버퍼 접근 (IOCP 완료 처리용)
#ifdef _WIN32
	IOContext &GetRecvContext() { return mRecvContext; }
	IOContext &GetSendContext() { return mSendContext; }
#endif

	// English: Virtual event handlers (override in derived classes)
	// 한글: 가상 이벤트 핸들러 (파생 클래스에서 오버라이드)
	virtual void OnConnected() {}
	virtual void OnDisconnected() {}
	virtual void OnRecv(const char *data, uint32_t size) {}

  private:
	// English: Internal send processing
	// 한글: 내부 전송 처리
	void FlushSendQueue();
	bool PostSend();

  private:
	Utils::ConnectionId mId;
	SocketHandle mSocket;
	SessionState mState;

	// English: Time tracking
	// 한글: 시간 추적
	Utils::Timestamp mConnectTime;
	Utils::Timestamp mLastPingTime;
	uint32_t mPingSequence;

	// English: IO contexts (Windows IOCP)
	// 한글: IO 컨텍스트 (Windows IOCP)
#ifdef _WIN32
	IOContext mRecvContext;
	IOContext mSendContext;
#endif

	// English: Send queue with lock contention optimization
	// 한글: Lock 경합 최적화가 적용된 전송 큐
	std::queue<std::vector<char>> mSendQueue;
	std::mutex mSendMutex;
	std::atomic<bool> mIsSending;

	// English: Fast-path optimization - queue size counter (lock-free read)
	// 한글: Fast-path 최적화 - 큐 크기 카운터 (lock-free 읽기)
	// Purpose: Avoid mutex lock when queue is likely empty
	// 목적: 큐가 비어있을 가능성이 높을 때 mutex lock 회피
	std::atomic<size_t> mSendQueueSize;
};

using SessionRef = std::shared_ptr<Session>;
using SessionWeakRef = std::weak_ptr<Session>;

} // namespace Network::Core
