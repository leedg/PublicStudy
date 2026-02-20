#pragma once

// English: Client session class for connection management
// ???: ???ㅼ뒦?????굿?域? ??ш낄援η뵳????????⑤９苑???嶺뚮ㅎ?????????

#include "../../Utils/NetworkUtils.h"
#include "AsyncIOProvider.h"
#include "PacketDefine.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>
#include <array>

namespace Network::Core
{
// =============================================================================
// English: Session state
// ???: ?嶺뚮ㅎ??????ㅺ컼??
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
// ???: IO ??????????
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
// ???: IOCP ????곷츉?????爾?????덉쉐 (Windows ??ш끽維??
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
// ???: ?嶺뚮ㅎ?????????
// =============================================================================

class Session : public std::enable_shared_from_this<Session>
{
	// English: NetworkEngine classes need access to PostSend for completion handling
	// ???: NetworkEngine ????????좊룆????ш끽維??癲ル슪?ｇ몭???????ш낄援??PostSend ???쒋닪????ш끽維??
	friend class BaseNetworkEngine;

  public:
	Session();
	virtual ~Session();

	// English: Lifecycle
	// ???: ??筌뤾쑨???낆뒩??곷뎨?
	void Initialize(Utils::ConnectionId id, SocketHandle socket);
	void Close();

	// English: Send packet
	// ???: ???????ш끽維뽬땻?
	void Send(const void *data, uint32_t size);

	template <typename T> void Send(const T &packet)
	{
		Send(&packet, sizeof(T));
	}

	// English: Post receive request to IOCP
	// ???: IOCP????筌뚯슜堉???釉먯뒜???濚밸Ŧ援욃ㅇ?
	bool PostRecv();

	// English: Accessors
	// ???: ???쒋닪???
	Utils::ConnectionId GetId() const { return mId; }
	SocketHandle GetSocket() const { return mSocket; }
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

	void SetAsyncProvider(AsyncIO::AsyncIOProvider *provider)
	{
		mAsyncProvider.store(provider, std::memory_order_release);
	}
	// English: Cross-platform recv buffer access
	// ???: ??繞??????????筌뚯슜堉??類???????쒋닪??
	char *GetRecvBuffer();
	const char *GetRecvBuffer() const;
	size_t GetRecvBufferSize() const;

	// English: Access recv buffer (for IOCP completion)
	// ???: ??筌뚯슜堉??類???????쒋닪??(IOCP ??ш끽維??癲ル슪?ｇ몭???
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
	// ???: ??좊읈??????濚???嶺뚮ㅎ?볠뤃??(????????????怨좊군??????곷츉??繹먮끏???
	virtual void OnConnected() {}
	virtual void OnDisconnected() {}
	virtual void OnRecv(const char *data, uint32_t size) {}

	// English: TCP stream reassembly - engine calls this with raw bytes
	// 한글: TCP 스트림 재조립 - 엔진이 원시 바이트로 이 메서드를 호출
	void ProcessRawRecv(const char *data, uint32_t size);

  private:
	// English: Internal send processing
	// ???: ???? ??ш끽維뽬땻?癲ル슪?ｇ몭??
	void FlushSendQueue();
	bool PostSend();

  private:
	Utils::ConnectionId mId;
	SocketHandle mSocket;
	std::atomic<SessionState> mState;

	// English: Time tracking
	// 한글: 시간 추적
	Utils::Timestamp mConnectTime;
	Utils::Timestamp mLastPingTime;
	std::atomic<uint32_t> mPingSequence;

	// English: IO contexts (Windows IOCP)
	// ???: IO ???爾?????덉쉐 (Windows IOCP)
#ifdef _WIN32
	IOContext mRecvContext;
	IOContext mSendContext;
#else
	// English: Recv buffer for POSIX platforms
	// ???: POSIX ??????繹먮끏裕???筌뚯슜堉??類????
	std::array<char, RECV_BUFFER_SIZE> mRecvBuffer{};
#endif

	// English: Send queue with lock contention optimization
	// ???: Lock ?濡ろ뜑???? 癲ル슔?됭짆???? ???ㅼ굣?????ш끽維뽬땻???
	std::queue<std::vector<char>> mSendQueue;
	std::mutex mSendMutex;
	std::atomic<bool> mIsSending;

	// English: Fast-path optimization - queue size counter (lock-free read)
	// ???: Fast-path 癲ル슔?됭짆???- ???????怨멸텭????(lock-free ??熬곣뱿逾?
	// Purpose: Avoid mutex lock when queue is likely empty
	// 癲ル슢?꾤땟?? ??? ????룹젂???源낃도 ??좊읈????묐빝???亦껋꼨援?キ???mutex lock ???⑤베猷?
	std::atomic<size_t> mSendQueueSize;

	// English: Async I/O provider — atomic pointer so Close() (any thread) and
	//          Send() (I/O thread) can race safely without a dedicated lock.
	//          Stored with release on set/clear and loaded with acquire before use.
	// 한글: 비동기 I/O 공급자 — Close()(임의 스레드)와 Send()(I/O 스레드) 간
	//       전용 락 없이 안전하게 경쟁하도록 atomic 포인터로 선언.
	std::atomic<AsyncIO::AsyncIOProvider*> mAsyncProvider;

	// English: TCP reassembly accumulation buffer + mutex + read offset
	//
	//   mRecvMutex  — serializes concurrent ProcessRawRecv calls.
	//                 PostRecv() is re-issued immediately after each completion, so a
	//                 second recv can complete before the first is processed by the logic
	//                 thread pool. Without this lock, two workers would race on the buffer.
	//
	//   mRecvAccumOffset — O(1) read pointer (position B pattern).
	//                      Instead of erasing (O(n) memmove) after every packet, we advance
	//                      an offset and compact only when the offset exceeds half the buffer.
	//                      Matches the same strategy used in TestServer::DBRecvLoop.
	//
	// 한글: TCP 재조립 누적 버퍼 + 뮤텍스 + 읽기 오프셋
	//
	//   mRecvMutex  — 동시 ProcessRawRecv 호출 직렬화.
	//                 PostRecv() 즉시 재발행으로 두 번째 recv가 먼저 완료될 수 있음.
	//
	//   mRecvAccumOffset — O(1) 읽기 포인터.
	//                      패킷마다 erase(O(n) memmove) 대신 오프셋만 전진하고,
	//                      오프셋이 버퍼 절반을 초과하면 compact.
	//                      TestServer::DBRecvLoop의 mDBRecvOffset 전략과 동일.
	std::vector<char> mRecvAccumBuffer;
	size_t            mRecvAccumOffset{0};
	std::mutex        mRecvMutex;
};

using SessionRef = std::shared_ptr<Session>;
using SessionWeakRef = std::weak_ptr<Session>;

} // namespace Network::Core
