#pragma once

// Client session class for connection management

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
// Session state
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
// IO operation type
// =============================================================================

enum class IOType : uint8_t
{
	Accept,
	Recv,
	Send,
	Disconnect,
};

// =============================================================================
// IOCP overlapped context (Windows only)
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
// Session class
// =============================================================================

class Session : public std::enable_shared_from_this<Session>
{
	// NetworkEngine classes need access to PostSend for completion handling
	friend class BaseNetworkEngine;

  public:
	Session();
	virtual ~Session();

	// Lifecycle
	void Initialize(Utils::ConnectionId id, SocketHandle socket);
	void Close();

	// Block until all in-flight AsyncScope tasks have completed.
	//          MUST be called between Close() and Reset() when returning a pool session.
	//          Close() calls mAsyncScope.Cancel() (skips pending tasks) but does NOT wait;
	//          pool sessions never call ~Session() so the RAII drain in ~AsyncScope()
	//          never fires. This method closes that gap.
	void WaitForPendingTasks();

	// Reset session state for pool reuse. Call after Close() + WaitForPendingTasks()
	//          and before re-Initialize(). Clears ID, state, counters, and recv accum buffers.
	//          mAsyncProvider is cleaned in Close(). mRecvAccumBuffer is cleared here
	//          (not in Close()) — see Close() comment for race rationale.
	void Reset();

	// Send result — returned by Send() to give the caller backpressure feedback.
	enum class SendResult : uint8_t
	{
		Ok,              // Packet enqueued/sent successfully
		QueueFull,       // Send queue above backpressure threshold
		NotConnected,    // Session not connected
		InvalidArgument, // Oversized or null packet — do not retry
	};

	// Send packet. Returns SendResult for backpressure feedback.
	SendResult Send(const void *data, uint32_t size);

	template <typename T> SendResult Send(const T &packet)
	{
		return Send(&packet, sizeof(T));
	}

	// Post receive request to IOCP
	bool PostRecv();

	// Accessors
	Utils::ConnectionId GetId() const { return mId; }
	SocketHandle GetSocket() const { return mSocket.load(std::memory_order_acquire); }
	SessionState GetState() const { return mState.load(std::memory_order_acquire); }
	bool IsConnected() const { return mState.load(std::memory_order_acquire) == SessionState::Connected; }

	Utils::Timestamp GetConnectTime() const { return mConnectTime; }
	Utils::Timestamp GetLastPingTime() const { return mLastPingTime; }
	void SetLastPingTime(Utils::Timestamp time) { mLastPingTime = time; }

	// Ping sequence — atomic to prevent race between ping timer thread and I/O thread
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
	// Cross-platform recv buffer access
	char *GetRecvBuffer();
	const char *GetRecvBuffer() const;
	size_t GetRecvBufferSize() const;

	// Access recv buffer (for IOCP completion)
#ifdef _WIN32
	IOContext &GetRecvContext() { return mRecvContext; }
	IOContext &GetSendContext() { return mSendContext; }

	// Resolve IO type by OVERLAPPED pointer without dereferencing it.
	//          Used by IOCP completion path to avoid touching freed memory.
	static bool TryResolveIOType(const OVERLAPPED *overlapped, IOType &outType);
#endif

	// Virtual event handlers (override in derived classes)
	virtual void OnConnected() {}
	virtual void OnDisconnected() {}
	virtual void OnRecv(const char *data, uint32_t size)
	{
		if (mOnRecvCb) mOnRecvCb(this, data, size);
	}

	// Per-session recv callback — set once in SessionManager::CreateSession via
	//          SetSessionConfigurator, before PostRecv() is issued. Cleared in Reset().
	//          Signature includes Session* so the handler can call session->Send() without
	//          capturing a raw pointer in the closure.
	using OnRecvCallback = std::function<void(Session*, const char*, uint32_t)>;
	void SetOnRecv(OnRecvCallback cb);

	// TCP stream reassembly - engine calls this with raw bytes
	void ProcessRawRecv(const char *data, uint32_t size);

  private:
	// Internal send processing
	void FlushSendQueue();
	bool PostSend();
	SocketHandle GetInvalidSocket() const;

  private:
	Utils::ConnectionId mId;
	std::atomic<SocketHandle> mSocket;
	std::atomic<SessionState> mState;

	// Time tracking
	Utils::Timestamp mConnectTime;
	Utils::Timestamp mLastPingTime;
	std::atomic<uint32_t> mPingSequence;

	// IO contexts (Windows IOCP)
#ifdef _WIN32
	IOContext mRecvContext;
	IOContext mSendContext;
#else
	// Recv buffer for POSIX platforms
	std::array<char, RECV_BUFFER_SIZE> mRecvBuffer{};
#endif

	// Send queue with lock contention optimization.
	//          IOCP path (Windows): uses SendRequest referencing a pool slot (0 alloc).
	//          Other platforms: uses vector<char> (unchanged).
#ifdef _WIN32
	struct SendRequest
	{
		size_t   slotIdx; // index into SendBufferPool
		uint32_t size;    // payload byte count
	};
	std::queue<SendRequest> mSendQueue;
	size_t mCurrentSendSlotIdx; // in-flight slot index (~0 = none)
#else
	std::queue<std::vector<char>> mSendQueue;
#endif
	std::mutex mSendMutex;
	std::atomic<bool> mIsSending;

	// Fast-path optimization - queue size counter (lock-free read)
	// Purpose: Avoid mutex lock when queue is likely empty
	std::atomic<size_t> mSendQueueSize;

	// Async I/O provider — protected by mSendMutex.
	//          SetAsyncProvider(), Close(), Send() RIO path, and PostSend() POSIX
	//          path all lock mSendMutex before reading/writing this field.
	//          Copy the shared_ptr under the lock, then use the snapshot outside
	//          the lock to avoid holding mSendMutex during actual I/O calls.
	std::shared_ptr<AsyncIO::AsyncIOProvider> mAsyncProvider;

	// TCP reassembly accumulation buffer + read offset.
	//
	//   mRecvMutex removed — serialization is now guaranteed by KeyedDispatcher affinity.
	//   Same sessionId always routes to the same worker thread, so ProcessRawRecv calls
	//   for a given session are inherently sequential (no concurrent workers).
	//
	//   mRecvAccumOffset — O(1) read pointer (position B pattern).
	//                      Instead of erasing (O(n) memmove) after every packet, we advance
	//                      an offset and compact only when the offset exceeds half the buffer.
	//
	//
	//
	std::vector<char> mRecvAccumBuffer;
	size_t            mRecvAccumOffset{0};

	// Reusable batch buffer for ProcessRawRecv general path.
	//          Reserved in Initialize() to amortise allocations across calls.
	//          No mutex needed — KeyedDispatcher affinity serialises all
	//          ProcessRawRecv calls for the same session on the same worker.
	//          Swapped with a local variable before dispatching OnRecv.
	std::vector<char> mRecvBatchBuf;

	// Application-level recv callback. Set once before PostRecv() in
	//          SessionManager::CreateSession (happens-before first recv completion).
	//          Cleared in Reset() so the slot can be reused without stale captures.
	OnRecvCallback mOnRecvCb;

	// Async scope for cooperative cancellation of queued logic tasks.
	//          BaseNetworkEngine calls mAsyncScope.Submit(...) instead of Dispatch() directly,
	//          so that tasks queued after Close() are silently skipped.
	//          RAII dtor calls Cancel() + WaitForDrain() ensuring no tasks run after Session dtor.
	Network::Concurrency::AsyncScope mAsyncScope;
};

using SessionRef = std::shared_ptr<Session>;
using SessionWeakRef = std::weak_ptr<Session>;

} // namespace Network::Core
