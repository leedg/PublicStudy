// English: Session class implementation
// ?쒓?: Session ?대옒??援ы쁽

#include "Session.h"
#include <cstring>
#include <iostream>
#include <sstream>
#ifndef _WIN32
#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace Network::Core
{

Session::Session()
	: mId(0), mSocket(
#ifdef _WIN32
				  INVALID_SOCKET
#else
				  -1
#endif
				  ),
		  mState(SessionState::None), mConnectTime(0), mLastPingTime(0),
		  mPingSequence(0), mIsSending(false), mSendQueueSize(0)
#ifdef _WIN32
		  ,
		  mAsyncProvider(nullptr),
		  mRecvContext(IOType::Recv),
		  mSendContext(IOType::Send)
#endif
{
}

Session::~Session() { Close(); }

void Session::Initialize(Utils::ConnectionId id, SocketHandle socket)
{
	mId = id;
	mSocket = socket;
	mState = SessionState::Connected;
	mConnectTime = Utils::Timer::GetCurrentTimestamp();
	mLastPingTime = mConnectTime;
	mPingSequence = 0;
	mIsSending = false;
	mSendQueueSize.store(0, std::memory_order_relaxed);
#ifdef _WIN32
	mAsyncProvider = nullptr;
#endif

	Utils::Logger::Info("Session initialized - ID: " + std::to_string(mId));
}

void Session::Close()
{
	if (mState == SessionState::Disconnected)
	{
		return;
	}

	mState = SessionState::Disconnected;
#ifdef _WIN32
	mAsyncProvider = nullptr;
#endif

	if (mSocket !=
#ifdef _WIN32
		INVALID_SOCKET
#else
		-1
#endif
	)
	{
#ifdef _WIN32
		closesocket(mSocket);
		mSocket = INVALID_SOCKET;
#else
		close(mSocket);
		mSocket = -1;
#endif
	}

	// English: Clear send queue
	// ?쒓?: ?꾩넚 ??鍮꾩슦湲?
	{
		std::lock_guard<std::mutex> lock(mSendMutex);
		while (!mSendQueue.empty())
		{
			mSendQueue.pop();
		}
		mSendQueueSize.store(0, std::memory_order_relaxed);
	}

	Utils::Logger::Info("Session closed - ID: " + std::to_string(mId));
}

void Session::Send(const void *data, uint32_t size)
{
#ifndef _WIN32
	if (!IsConnected() || data == nullptr || size == 0)
	{
		return;
	}

	const char *ptr = static_cast<const char *>(data);
	uint32_t remaining = size;
	int flags = 0;
#ifdef MSG_NOSIGNAL
	flags = MSG_NOSIGNAL;
#endif

	while (remaining > 0)
	{
		ssize_t sent = ::send(mSocket, ptr, remaining, flags);
		if (sent > 0)
		{
			remaining -= static_cast<uint32_t>(sent);
			ptr += sent;
			continue;
		}

		if (sent < 0 && errno == EINTR)
		{
			continue;
		}

		Utils::Logger::Error("send failed - errno: " + std::to_string(errno));
		break;
	}

	return;
#else
	if (!IsConnected() || data == nullptr || size == 0)
	{
		return;
	}

	if (mAsyncProvider)
	{
		auto error = mAsyncProvider->SendAsync(
			mSocket, data, size, static_cast<AsyncIO::RequestContext>(mId));
		if (error != AsyncIO::AsyncIOError::Success)
		{
			Utils::Logger::Error(
				"RIO send failed - Session: " + std::to_string(mId) +
				", Socket: " +
				std::to_string(static_cast<unsigned long long>(mSocket)) +
				", Error: " + std::string(mAsyncProvider->GetLastError()));
		}
		else
		{
			(void)mAsyncProvider->FlushRequests();
		}
		return;
	}

	// English: Lock contention optimization using atomic queue size counter
	// ?쒓?: Atomic ???ш린 移댁슫?곕? ?ъ슜??Lock 寃쏀빀 理쒖쟻??
	//
	// Performance optimization strategy:
	// - Fast path: Check mSendQueueSize (lock-free) before acquiring mutex
	// - Slow path: Only acquire mutex when actually enqueuing data
	// - Benefit: Reduces lock contention when Send() is called frequently
	//
	// ?깅뒫 理쒖쟻???꾨왂:
	// - Fast path: mutex ?띾뱷 ?꾩뿉 mSendQueueSize瑜??뺤씤 (lock-free)
	// - Slow path: ?ㅼ젣濡??곗씠?곕? ?명걧???뚮쭔 mutex ?띾뱷
	// - ?댁젏: Send()媛 ?먯＜ ?몄텧????lock 寃쏀빀 媛먯냼

	// English: Prepare buffer outside of lock (minimize critical section)
	// ?쒓?: Lock ?몃??먯꽌 踰꾪띁 以鍮?(?꾧퀎 ?곸뿭 理쒖냼??
	std::vector<char> buffer(size);
	std::memcpy(buffer.data(), data, size);

	// English: Enqueue with atomic size tracking
	// ?쒓?: Atomic ?ш린 異붿쟻怨??④퍡 ?명걧
	{
		std::lock_guard<std::mutex> lock(mSendMutex);
		mSendQueue.push(std::move(buffer));

		// English: Increment queue size atomically with release so PostSend's
		//          acquire load sees the enqueued data
		// ?쒓?: PostSend??acquire load媛 ?명걧???곗씠?곕? 蹂????덈룄濡?release濡?利앷?
		mSendQueueSize.fetch_add(1, std::memory_order_release);
	}

	// English: Always try to flush (CAS inside will prevent double send)
	// ?쒓?: ??긽 ?뚮윭???쒕룄 (?대? CAS媛 以묐났 ?꾩넚 諛⑹?)
	FlushSendQueue();
#endif
}

void Session::FlushSendQueue()
{
	// English: CAS to prevent concurrent sends
	// ?쒓?: CAS濡??숈떆 ?꾩넚 諛⑹?
	bool expected = false;
	if (!mIsSending.compare_exchange_strong(expected, true))
	{
		return;
	}

	PostSend();
}

bool Session::PostSend()
{
#ifdef _WIN32
	// English: Fast path - acquire load pairs with release store in Send()
	//          so we see all enqueued items before deciding queue is empty
	// ?쒓?: Fast path - Send()??release store? ?띿쓣 ?대（??acquire load濡?
	//       ?먭? 鍮꾩뼱?덈떎怨??먮떒?섍린 ?꾩뿉 ?명걧??紐⑤뱺 ??ぉ??蹂????덉쓬
	if (mSendQueueSize.load(std::memory_order_acquire) == 0)
	{
		// English: Queue is empty, release sending flag and return
		// ?쒓?: ?먭? 鍮꾩뼱?덉쓬, ?꾩넚 ?뚮옒洹??댁젣 ??諛섑솚
		mIsSending.store(false, std::memory_order_release);
		return true;
	}

	std::vector<char> dataToSend;

	{
		std::lock_guard<std::mutex> lock(mSendMutex);

		// English: Double-check queue after acquiring lock (TOCTOU prevention)
		// ?쒓?: Lock ?띾뱷 ?????ы솗??(TOCTOU 諛⑹?)
		if (mSendQueue.empty())
		{
			// English: No more data to send, release flag atomically
			// ?쒓?: ???댁긽 ?꾩넚???곗씠???놁쓬, atomic?쇰줈 ?뚮옒洹??댁젣
			mIsSending.store(false, std::memory_order_release);
			return true;
		}

		dataToSend = std::move(mSendQueue.front());
		mSendQueue.pop();

		// English: Decrement queue size atomically
		// ?쒓?: Atomic?쇰줈 ???ш린 媛먯냼
		mSendQueueSize.fetch_sub(1, std::memory_order_release);
	}

	mSendContext.Reset();
	std::memcpy(mSendContext.buffer, dataToSend.data(), dataToSend.size());
	mSendContext.wsaBuf.buf = mSendContext.buffer;
	mSendContext.wsaBuf.len = static_cast<ULONG>(dataToSend.size());

	DWORD bytesSent = 0;
	int result = WSASend(mSocket, &mSendContext.wsaBuf, 1, &bytesSent, 0,
						 &mSendContext, nullptr);

	if (result == SOCKET_ERROR)
	{
		int error = WSAGetLastError();
		if (error != WSA_IO_PENDING)
		{
			Utils::Logger::Error("WSASend failed - Error: " +
								 std::to_string(error));
			// English: Release flag atomically on error
			// ?쒓?: ?먮윭 ??atomic?쇰줈 ?뚮옒洹??댁젣
			mIsSending.store(false, std::memory_order_release);
			return false;
		}
	}

	return true;
#else
	// English: Linux/macOS implementation (placeholder)
	// ?쒓?: Linux/macOS 援ы쁽 (?뚮젅?댁뒪???
	mIsSending.store(false, std::memory_order_release);
	return false;
#endif
}

bool Session::PostRecv()
{
#ifdef _WIN32
	if (!IsConnected())
	{
		return false;
	}

	mRecvContext.Reset();
	mRecvContext.wsaBuf.buf = mRecvContext.buffer;
	mRecvContext.wsaBuf.len = sizeof(mRecvContext.buffer);

	DWORD bytesReceived = 0;
	DWORD flags = 0;

	int result = WSARecv(mSocket, &mRecvContext.wsaBuf, 1, &bytesReceived,
						 &flags, &mRecvContext, nullptr);

	if (result == SOCKET_ERROR)
	{
		int error = WSAGetLastError();
		if (error != WSA_IO_PENDING)
		{
			Utils::Logger::Error("WSARecv failed - Error: " +
								 std::to_string(error));
			return false;
		}
	}

	return true;
#else
	return false;
#endif
}

char *Session::GetRecvBuffer()
{
#ifdef _WIN32
	return mRecvContext.buffer;
#else
	return mRecvBuffer.data();
#endif
}

const char *Session::GetRecvBuffer() const
{
#ifdef _WIN32
	return mRecvContext.buffer;
#else
	return mRecvBuffer.data();
#endif
}

size_t Session::GetRecvBufferSize() const
{
#ifdef _WIN32
	return sizeof(mRecvContext.buffer);
#else
	return mRecvBuffer.size();
#endif
}

} // namespace Network::Core
