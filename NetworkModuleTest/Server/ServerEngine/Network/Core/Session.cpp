// English: Session class implementation
// 한글: Session 클래스 구현

#include "Session.h"
#include <cstring>
#include <iostream>
#include <sstream>

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
		  mRecvContext(IOType::Recv), mSendContext(IOType::Send)
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

	Utils::Logger::Info("Session initialized - ID: " + std::to_string(mId));
}

void Session::Close()
{
	if (mState == SessionState::Disconnected)
	{
		return;
	}

	mState = SessionState::Disconnected;

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
	// 한글: 전송 큐 비우기
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
	if (!IsConnected() || data == nullptr || size == 0)
	{
		return;
	}

	// English: Lock contention optimization using atomic queue size counter
	// 한글: Atomic 큐 크기 카운터를 사용한 Lock 경합 최적화
	//
	// Performance optimization strategy:
	// - Fast path: Check mSendQueueSize (lock-free) before acquiring mutex
	// - Slow path: Only acquire mutex when actually enqueuing data
	// - Benefit: Reduces lock contention when Send() is called frequently
	//
	// 성능 최적화 전략:
	// - Fast path: mutex 획득 전에 mSendQueueSize를 확인 (lock-free)
	// - Slow path: 실제로 데이터를 인큐할 때만 mutex 획득
	// - 이점: Send()가 자주 호출될 때 lock 경합 감소

	// English: Prepare buffer outside of lock (minimize critical section)
	// 한글: Lock 외부에서 버퍼 준비 (임계 영역 최소화)
	std::vector<char> buffer(size);
	std::memcpy(buffer.data(), data, size);

	// English: Enqueue with atomic size tracking
	// 한글: Atomic 크기 추적과 함께 인큐
	{
		std::lock_guard<std::mutex> lock(mSendMutex);
		mSendQueue.push(std::move(buffer));

		// English: Increment queue size atomically (enables lock-free size check)
		// 한글: Atomic으로 큐 크기 증가 (lock-free 크기 확인 가능)
		mSendQueueSize.fetch_add(1, std::memory_order_relaxed);
	}

	// English: Always try to flush (CAS inside will prevent double send)
	// 한글: 항상 플러시 시도 (내부 CAS가 중복 전송 방지)
	FlushSendQueue();
}

void Session::FlushSendQueue()
{
	// English: CAS to prevent concurrent sends
	// 한글: CAS로 동시 전송 방지
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
	// English: Fast path - check queue size without lock (lock-free optimization)
	// 한글: Fast path - Lock 없이 큐 크기 확인 (lock-free 최적화)
	// Performance: This atomic load is much faster than acquiring mutex
	// 성능: 이 atomic load는 mutex 획득보다 훨씬 빠름
	if (mSendQueueSize.load(std::memory_order_relaxed) == 0)
	{
		// English: Queue is empty, release sending flag and return
		// 한글: 큐가 비어있음, 전송 플래그 해제 후 반환
		mIsSending.store(false, std::memory_order_release);
		return true;
	}

	std::vector<char> dataToSend;

	{
		std::lock_guard<std::mutex> lock(mSendMutex);

		// English: Double-check queue after acquiring lock (TOCTOU prevention)
		// 한글: Lock 획득 후 큐 재확인 (TOCTOU 방지)
		if (mSendQueue.empty())
		{
			// English: No more data to send, release flag atomically
			// 한글: 더 이상 전송할 데이터 없음, atomic으로 플래그 해제
			mIsSending.store(false, std::memory_order_release);
			return true;
		}

		dataToSend = std::move(mSendQueue.front());
		mSendQueue.pop();

		// English: Decrement queue size atomically
		// 한글: Atomic으로 큐 크기 감소
		mSendQueueSize.fetch_sub(1, std::memory_order_relaxed);
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
			// 한글: 에러 시 atomic으로 플래그 해제
			mIsSending.store(false, std::memory_order_release);
			return false;
		}
	}

	return true;
#else
	// English: Linux/macOS implementation (placeholder)
	// 한글: Linux/macOS 구현 (플레이스홀더)
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

} // namespace Network::Core
