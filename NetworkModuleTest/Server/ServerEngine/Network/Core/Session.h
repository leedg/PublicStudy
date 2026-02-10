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
	SessionState GetState() const { return mState; }
	bool IsConnected() const { return mState == SessionState::Connected; }

	Utils::Timestamp GetConnectTime() const { return mConnectTime; }
	Utils::Timestamp GetLastPingTime() const { return mLastPingTime; }
	void SetLastPingTime(Utils::Timestamp time) { mLastPingTime = time; }
	uint32_t GetPingSequence() const { return mPingSequence; }
	void IncrementPingSequence() { ++mPingSequence; }

#ifdef _WIN32
	void SetUseSynchronousSend(bool enable)
	{
		mUseSynchronousSend.store(enable, std::memory_order_relaxed);
	}
	bool IsUsingSynchronousSend() const
	{
		return mUseSynchronousSend.load(std::memory_order_relaxed);
	}
	void SetAsyncProvider(AsyncIO::AsyncIOProvider *provider)
	{
		mAsyncProvider = provider;
	}
#endif
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
#endif

	// English: Virtual event handlers (override in derived classes)
	// ???: ??좊읈??????濚???嶺뚮ㅎ?볠뤃??(????????????怨좊군??????곷츉??繹먮끏???
	virtual void OnConnected() {}
	virtual void OnDisconnected() {}
	virtual void OnRecv(const char *data, uint32_t size) {}

  private:
	// English: Internal send processing
	// ???: ???? ??ш끽維뽬땻?癲ル슪?ｇ몭??
	void FlushSendQueue();
	bool PostSend();

  private:
	Utils::ConnectionId mId;
	SocketHandle mSocket;
	SessionState mState;

	// English: Time tracking
	// ???: ??癰?????⑤베毓??
	Utils::Timestamp mConnectTime;
	Utils::Timestamp mLastPingTime;
	uint32_t mPingSequence;

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
#ifdef _WIN32
	std::atomic<bool> mUseSynchronousSend;
	AsyncIO::AsyncIOProvider *mAsyncProvider;
#endif
};

using SessionRef = std::shared_ptr<Session>;
using SessionWeakRef = std::weak_ptr<Session>;

} // namespace Network::Core
