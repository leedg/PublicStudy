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
		  mPingSequence(0), mIsSending(false), mSendQueueSize(0),
		  mAsyncProvider(nullptr)
#ifdef _WIN32
		  ,
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
	mState.store(SessionState::Connected, std::memory_order_release);
	mConnectTime = Utils::Timer::GetCurrentTimestamp();
	mLastPingTime = mConnectTime;
	mPingSequence = 0;
	mIsSending = false;
	mSendQueueSize.store(0, std::memory_order_relaxed);
	mAsyncProvider = nullptr;
	mRecvAccumBuffer.clear();

	Utils::Logger::Info("Session initialized - ID: " + std::to_string(mId));
}

void Session::Close()
{
	// English: Atomic exchange prevents TOCTOU double-close race
	// 한글: atomic exchange로 TOCTOU 이중 닫기 경쟁 방지
	SessionState prev = mState.exchange(SessionState::Disconnected, std::memory_order_acq_rel);
	if (prev == SessionState::Disconnected)
	{
		return;
	}

	mAsyncProvider = nullptr;

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
	if (!IsConnected() || data == nullptr || size == 0)
	{
		return;
	}

#ifdef _WIN32
	if (mAsyncProvider)
	{
		// English: RIO path - delegate directly to async provider
		// 한글: RIO 경로 - 비동기 공급자에 직접 위임
		auto error = mAsyncProvider->SendAsync(
			mSocket, data, size, static_cast<AsyncIO::RequestContext>(mId));
		if (error != AsyncIO::AsyncIOError::Success)
		{
			Utils::Logger::Error(
				"RIO send failed - Session: " + std::to_string(mId) +
				", Error: " + std::string(mAsyncProvider->GetLastError()));
		}
		else
		{
			(void)mAsyncProvider->FlushRequests();
		}
		return;
	}
#endif

	// English: Back-pressure: drop packet if send queue is full
	// 한글: 백압력: 송신 큐가 가득 찬 경우 패킷 드롭
	if (mSendQueueSize.load(std::memory_order_relaxed) >= MAX_SEND_QUEUE_DEPTH)
	{
		Utils::Logger::Warn("Send queue full - packet dropped (Session: " +
							std::to_string(mId) + ")");
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
	// English: Fast path - check queue size before acquiring lock
	// 한글: Fast path - 락 획득 전 큐 크기 확인
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

#ifdef _WIN32
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
			// English: Socket error - release flag then close session
			// 한글: 소켓 오류 - 플래그 해제 후 세션 종료
			// ?쒓?: ?먮윭 ??atomic?쇰줈 ?뚮옒洹??댁젣
			mIsSending.store(false, std::memory_order_release);
			Close();
			return false;
		}
	}

	return true;
#else
	// English: Linux/macOS - use async provider for non-blocking send
	if (!mAsyncProvider)
	{
		mIsSending.store(false, std::memory_order_release);
		return false;
	}

	auto sendError = mAsyncProvider->SendAsync(
		mSocket, dataToSend.data(), dataToSend.size(),
		static_cast<AsyncIO::RequestContext>(mId));

	if (sendError != AsyncIO::AsyncIOError::Success)
	{
		Utils::Logger::Error(
			"SendAsync failed - Session: " + std::to_string(mId) +
			", Error: " + std::string(mAsyncProvider->GetLastError()));
		mIsSending.store(false, std::memory_order_release);
		Close();
		return false;
	}

	// mIsSending stays true until send completion fires ProcessSendCompletion
	return true;
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

void Session::ProcessRawRecv(const char *data, uint32_t size)
{
	// English: Serialize concurrent calls — PostRecv() is re-issued immediately after each
	//          completion, so a second recv can complete before the first is processed by the
	//          logic thread pool, causing two workers to run here simultaneously.
	//          Without this lock, mRecvAccumBuffer would be accessed from two threads
	//          concurrently, corrupting the buffer and making hdr a dangling pointer
	//          (manifests as hdr->size = 0xDDDD, the MSVC freed-memory fill pattern).
	// 한글: 동시 호출 직렬화 — PostRecv()가 완료 즉시 재호출되므로 두 번째 recv가
	//       첫 번째보다 먼저 LogicThreadPool에서 실행될 수 있음.
	//       이 락 없이는 mRecvAccumBuffer에 두 스레드가 동시 접근하여 hdr 댕글링 포인터
	//       (hdr->size = 0xDDDD) 크래시 발생.
	std::lock_guard<std::mutex> recvLock(mRecvMutex);

	// English: Guard against oversized accumulation (slow-loris / flood defense).
	//          Threshold must be > (MAX_PACKET_SIZE - 1) + RECV_BUFFER_SIZE so that
	//          a legitimate partial packet tail + one full recv never triggers a false reset.
	//          MAX_PACKET_SIZE (4096) alone is too small: RECV_BUFFER_SIZE (8192) exceeds it,
	//          so a single large recv on a non-empty buffer would corrupt the stream.
	// 한글: 과도한 누적 방어 (느린 공격 / 플러드 방어).
	//       임계값은 (MAX_PACKET_SIZE - 1) + RECV_BUFFER_SIZE 보다 커야 함.
	//       부분 패킷 꼬리 + 단일 recv 최대치가 정상 범위를 벗어나지 않도록.
	//       MAX_PACKET_SIZE(4096) 단독 사용 시: RECV_BUFFER_SIZE(8192)가 이를 초과하므로
	//       빈 버퍼에서도 단일 recv가 가드를 발동시켜 스트림을 오염시킬 수 있음.
	constexpr size_t kMaxAccumSize = MAX_PACKET_SIZE * 4;  // 16KB — safely above 4095 + 8192
	if (mRecvAccumBuffer.size() + size > kMaxAccumSize)
	{
		Utils::Logger::Warn("Recv accumulation buffer overflow, closing session - Session: " +
							std::to_string(mId));
		mRecvAccumBuffer.clear();
		Close();  // English: Stream state is unrecoverable without a full reconnect
				  // 한글: 재연결 없이는 스트림 상태 복구 불가 — 세션 강제 종료
		return;
	}

	mRecvAccumBuffer.insert(mRecvAccumBuffer.end(), data, data + size);

	while (mRecvAccumBuffer.size() >= sizeof(PacketHeader))
	{
		const auto *hdr = reinterpret_cast<const PacketHeader *>(mRecvAccumBuffer.data());

		// English: Validate packet size bounds
		// 한글: 패킷 크기 경계 검증
		if (hdr->size < sizeof(PacketHeader) || hdr->size > MAX_PACKET_SIZE)
		{
			Utils::Logger::Warn("Invalid packet size " + std::to_string(hdr->size) +
								", resetting stream - Session: " + std::to_string(mId));
			mRecvAccumBuffer.clear();
			break;
		}

		if (mRecvAccumBuffer.size() < hdr->size)
		{
			break; // English: Partial packet, wait for more data / 한글: 불완전한 패킷, 추가 데이터 대기
		}

		// English: Capture size before OnRecv() — hdr points into mRecvAccumBuffer.data()
		//          and OnRecv() must not touch mRecvAccumBuffer (lock is held), but capturing
		//          the size up-front makes the intent explicit and guards against future changes.
		// 한글: OnRecv() 호출 전 size 캡처 — hdr는 mRecvAccumBuffer.data() 내부를 가리키며,
		//       OnRecv() 중 버퍼가 수정되지 않도록 락이 보호하지만, 명시적 캡처로 안전성 강화.
		const uint16_t packetSize = hdr->size;
		OnRecv(mRecvAccumBuffer.data(), packetSize);
		mRecvAccumBuffer.erase(mRecvAccumBuffer.begin(),
							   mRecvAccumBuffer.begin() + packetSize);
	}
}

} // namespace Network::Core
