// English: Session class implementation
// ?쒓?: Session ?대옒??援ы쁽

#include "Session.h"
#include <cstring>
#include <iostream>
#include <sstream>
#include <unordered_map>
#ifndef _WIN32
#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace Network::Core
{

#ifdef _WIN32
namespace
{
std::mutex gIOTypeRegistryMutex;
std::unordered_map<const OVERLAPPED *, IOType> gIOTypeRegistry;
}
#endif

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
#ifdef _WIN32
	std::lock_guard<std::mutex> lock(gIOTypeRegistryMutex);
	gIOTypeRegistry[static_cast<const OVERLAPPED *>(&mRecvContext)] = IOType::Recv;
	gIOTypeRegistry[static_cast<const OVERLAPPED *>(&mSendContext)] = IOType::Send;
#endif
}

Session::~Session()
{
#ifdef _WIN32
	{
		std::lock_guard<std::mutex> lock(gIOTypeRegistryMutex);
		gIOTypeRegistry.erase(static_cast<const OVERLAPPED *>(&mRecvContext));
		gIOTypeRegistry.erase(static_cast<const OVERLAPPED *>(&mSendContext));
	}
#endif
	Close();
}

#ifdef _WIN32
bool Session::TryResolveIOType(const OVERLAPPED *overlapped, IOType &outType)
{
	if (overlapped == nullptr)
	{
		return false;
	}

	std::lock_guard<std::mutex> lock(gIOTypeRegistryMutex);
	const auto it = gIOTypeRegistry.find(overlapped);
	if (it == gIOTypeRegistry.end())
	{
		return false;
	}

	outType = it->second;
	return true;
}
#endif

void Session::Initialize(Utils::ConnectionId id, SocketHandle socket)
{
	mId = id;
	mSocket = socket;
	mState.store(SessionState::Connected, std::memory_order_release);
	mConnectTime = Utils::Timer::GetCurrentTimestamp();
	mLastPingTime = mConnectTime;
	mPingSequence.store(0, std::memory_order_relaxed);
	mIsSending = false;
	mSendQueueSize.store(0, std::memory_order_relaxed);
	mAsyncProvider.store(nullptr, std::memory_order_relaxed);
	mRecvAccumBuffer.clear();
	mRecvAccumOffset = 0;

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

	// English: Release-store nullptr so any concurrent Send() acquire-load sees it before
	//          the socket is closed below — prevents use of a closed/reassigned socket.
	// 한글: 아래 소켓 닫기 전에 concurrent Send()의 acquire-load가 nullptr을 볼 수 있도록
	//       release-store로 nullptr 설정.
	mAsyncProvider.store(nullptr, std::memory_order_release);

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

	// English: Clear send queue and recv reassembly state
	// 한글: 송신 큐와 수신 재조립 상태 초기화
	{
		std::lock_guard<std::mutex> lock(mSendMutex);
		while (!mSendQueue.empty())
		{
			mSendQueue.pop();
		}
		mSendQueueSize.store(0, std::memory_order_relaxed);
	}
	{
		std::lock_guard<std::mutex> recvLock(mRecvMutex);
		mRecvAccumBuffer.clear();
		mRecvAccumOffset = 0;
	}

	Utils::Logger::Info("Session closed - ID: " + std::to_string(mId));
}

void Session::Send(const void *data, uint32_t size)
{
	if (!IsConnected() || data == nullptr || size == 0)
	{
		return;
	}

	if (size > SEND_BUFFER_SIZE)
	{
		Utils::Logger::Warn("Send size exceeds SEND_BUFFER_SIZE - packet dropped (Session: " +
							std::to_string(mId) + ", Size: " + std::to_string(size) + ")");
		return;
	}

#ifdef _WIN32
	{
		// English: Acquire-load into a local snapshot so we avoid a race where
		//          Close() stores nullptr (release) between our IsConnected() check
		//          and the actual use of the pointer.
		// 한글: IsConnected() 체크와 실제 사용 사이에 Close()가 nullptr을 store하는
		//       race를 막기 위해 acquire-load로 로컬 스냅샷을 만든다.
		auto* provider = mAsyncProvider.load(std::memory_order_acquire);
		if (provider)
		{
			// English: RIO path - delegate directly to async provider
			// 한글: RIO 경로 - 비동기 공급자에 직접 위임
			auto error = provider->SendAsync(
				mSocket, data, size, static_cast<AsyncIO::RequestContext>(mId));
			if (error != AsyncIO::AsyncIOError::Success)
			{
				Utils::Logger::Error(
					"RIO send failed - Session: " + std::to_string(mId) +
					", Error: " + std::string(provider->GetLastError()));
			}
			else
			{
				(void)provider->FlushRequests();
			}
			return;
		}
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
	if (dataToSend.size() > sizeof(mSendContext.buffer))
	{
		Utils::Logger::Error("Send buffer overflow risk detected - closing session (Session: " +
							 std::to_string(mId) + ", Size: " +
							 std::to_string(dataToSend.size()) + ")");
		mIsSending.store(false, std::memory_order_release);
		Close();
		return false;
	}

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
	// English: Linux/macOS - acquire-load into local to avoid race with Close()
	// 한글: Close()와의 race 방지를 위해 acquire-load로 로컬 스냅샷 사용
	auto* provider = mAsyncProvider.load(std::memory_order_acquire);
	if (!provider)
	{
		mIsSending.store(false, std::memory_order_release);
		return false;
	}

	auto sendError = provider->SendAsync(
		mSocket, dataToSend.data(), dataToSend.size(),
		static_cast<AsyncIO::RequestContext>(mId));

	if (sendError != AsyncIO::AsyncIOError::Success)
	{
		Utils::Logger::Error(
			"SendAsync failed - Session: " + std::to_string(mId) +
			", Error: " + std::string(provider->GetLastError()));
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
	// English: Serialize concurrent calls.
	//          PostRecv() is re-issued immediately after each IOCP completion, so a second
	//          recv can complete before the first is processed by the logic thread pool —
	//          two workers run here simultaneously. Without this lock, mRecvAccumBuffer
	//          would be corrupted (manifests as hdr->size = 0xDDDD freed-memory pattern).
	// 한글: 동시 호출 직렬화.
	//       PostRecv()가 완료 즉시 재발행되므로 두 번째 recv가 먼저 LogicThreadPool에서
	//       실행될 수 있음. 이 락 없이는 버퍼 손상(hdr->size = 0xDDDD) 크래시 발생.
	std::lock_guard<std::mutex> recvLock(mRecvMutex);

	// English: Overflow guard (slow-loris / flood defense).
	//          Threshold > (MAX_PACKET_SIZE - 1) + RECV_BUFFER_SIZE so that a legitimate
	//          partial tail + one full recv never triggers a false reset.
	//          We measure unread bytes (size - offset) + incoming to avoid counting already-
	//          consumed bytes in the compaction-pending region.
	// 한글: 오버플로우 방어 (느린 공격 / 플러드 방어).
	//       미읽은 바이트(size - offset) + 수신량으로 판단해 이미 소비된 영역을 제외.
	constexpr size_t kMaxAccumSize = MAX_PACKET_SIZE * 4;  // 16 KB — safely above 4095 + 8192
	const size_t unread = mRecvAccumBuffer.size() - mRecvAccumOffset;
	if (unread + size > kMaxAccumSize)
	{
		Utils::Logger::Warn("Recv accumulation buffer overflow, closing session - Session: " +
							std::to_string(mId));
		mRecvAccumBuffer.clear();
		mRecvAccumOffset = 0;
		Close();  // English: Stream state unrecoverable without full reconnect
				  // 한글: 재연결 없이는 스트림 상태 복구 불가
		return;
	}

	mRecvAccumBuffer.insert(mRecvAccumBuffer.end(), data, data + size);

	// English: Parse complete packets using O(1) offset advance (position B strategy).
	//          Instead of erasing after each packet (O(n) memmove), we advance mRecvAccumOffset
	//          and compact only when the offset exceeds half the buffer — identical to
	//          TestServer::DBRecvLoop's mDBRecvOffset approach.
	// 한글: O(1) 오프셋 전진으로 완성된 패킷을 파싱 (position B 전략).
	//       패킷마다 erase(O(n) memmove) 대신 mRecvAccumOffset만 전진하고,
	//       오프셋이 버퍼 절반 초과 시 compact — TestServer::DBRecvLoop의 전략과 동일.
	while (mRecvAccumBuffer.size() - mRecvAccumOffset >= sizeof(PacketHeader))
	{
		const auto *hdr = reinterpret_cast<const PacketHeader *>(
			mRecvAccumBuffer.data() + mRecvAccumOffset);

		// English: Validate packet size bounds before reading payload
		// 한글: 페이로드 읽기 전 패킷 크기 경계 검증
		if (hdr->size < sizeof(PacketHeader) || hdr->size > MAX_PACKET_SIZE)
		{
			Utils::Logger::Warn("Invalid packet size " + std::to_string(hdr->size) +
								", resetting stream - Session: " + std::to_string(mId));
			mRecvAccumBuffer.clear();
			mRecvAccumOffset = 0;
			break;
		}

		// English: Partial packet — wait for more data
		// 한글: 불완전한 패킷 — 추가 데이터 대기
		if (mRecvAccumBuffer.size() - mRecvAccumOffset < hdr->size)
		{
			break;
		}

		// English: Capture packetSize before OnRecv() — hdr points into mRecvAccumBuffer
		//          and must not be touched during or after OnRecv() dispatch.
		// 한글: OnRecv() 호출 전 packetSize 캡처 — hdr는 mRecvAccumBuffer 내부 포인터.
		const uint16_t packetSize = hdr->size;
		OnRecv(mRecvAccumBuffer.data() + mRecvAccumOffset, packetSize);
		mRecvAccumOffset += packetSize;
	}

	// English: Compact: slide unread bytes to front when offset exceeds half the buffer.
	//          If all bytes were consumed, fast-clear without memmove.
	// 한글: Compact: 오프셋이 버퍼 절반 초과 시 미읽은 바이트를 앞으로 이동.
	//       모든 바이트가 소비된 경우 memmove 없이 빠르게 clear.
	if (mRecvAccumOffset >= mRecvAccumBuffer.size())
	{
		// English: Entire buffer consumed — O(1) fast reset
		// 한글: 버퍼 전체 소비 — O(1) 빠른 리셋
		mRecvAccumBuffer.clear();
		mRecvAccumOffset = 0;
	}
	else if (mRecvAccumOffset > mRecvAccumBuffer.size() / 2)
	{
		// English: Compact: erase consumed prefix to reclaim memory
		// 한글: Compact: 소비된 앞부분 제거로 메모리 회수
		mRecvAccumBuffer.erase(mRecvAccumBuffer.begin(),
							   mRecvAccumBuffer.begin() + static_cast<std::ptrdiff_t>(mRecvAccumOffset));
		mRecvAccumOffset = 0;
	}
}

} // namespace Network::Core
