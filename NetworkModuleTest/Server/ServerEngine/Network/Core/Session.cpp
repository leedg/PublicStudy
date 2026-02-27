// English: Session class implementation
// 한글: Session 클래스 구현

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

SocketHandle Session::GetInvalidSocket() const
{
#ifdef _WIN32
	return INVALID_SOCKET;
#else
	return -1;
#endif
}

void Session::Initialize(Utils::ConnectionId id, SocketHandle socket)
{
	mId = id;
	mSocket.store(socket, std::memory_order_release);
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
	// English: Release-to-Synchronize-store ensures: (1) all prior close-state stores
	//          visible to subsequent Send(); (2) socket-closure below visible after
	//          async provider clears; (3) sequential consistency with other threads.
	// 한글: Release-To-Synchronize-store로 (1) 이전 닫기 상태 모두 Send()에 보임,
	//      (2) 아래 소켓 닫기가 async provider 초기화 후 보임, (3) 다른 스레드와 일관성.
	mAsyncProvider.store(nullptr, std::memory_order_seq_cst);

	const SocketHandle socketToClose =
		mSocket.exchange(GetInvalidSocket(), std::memory_order_acq_rel);

	if (socketToClose != GetInvalidSocket())
	{
#ifdef _WIN32
		closesocket(socketToClose);
#else
		close(socketToClose);
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
		const SocketHandle socket = mSocket.load(std::memory_order_acquire);
		if (socket == GetInvalidSocket())
		{
			return;
		}

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
				socket, data, size, static_cast<AsyncIO::RequestContext>(mId));
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
	// 한글: Atomic 큐 크기 카운터를 사용한 Lock 경합 최적화
	//
	// Performance optimization strategy:
	// - Fast path: Check mSendQueueSize (lock-free) before acquiring mutex
	// - Slow path: Only acquire mutex when actually enqueuing data
	// - Benefit: Reduces lock contention when Send() is called frequently
	//
	// 성능 최적화 전략:
	// - Fast path: mutex 획득 전에 mSendQueueSize 확인 (lock-free)
	// - Slow path: 실제로 데이터를 큐에 넣을 때에만 mutex 획득
	// - 이점: Send()가 자주 호출될 때 lock 경합 감소

	// English: Prepare buffer outside of lock (minimize critical section)
	// 한글: Lock 밖에서 버퍼 준비 (임계 구역 최소화)
	std::vector<char> buffer(size);
	std::memcpy(buffer.data(), data, size);

	// English: Enqueue with atomic size tracking
	// 한글: Atomic 크기 추적과 함께 큐에 삽입
	{
		std::lock_guard<std::mutex> lock(mSendMutex);
		mSendQueue.push(std::move(buffer));

		// English: Increment queue size atomically with release so PostSend's
		//          acquire load sees the enqueued data
		// 한글: PostSend의 acquire load가 삽입된 데이터를 볼 수 있도록 release로 증가
		mSendQueueSize.fetch_add(1, std::memory_order_release);
	}

	// English: Always try to flush (CAS inside will prevent double send)
	// 한글: 항상 플러시 시도 (내부 CAS가 이중 전송 방지)
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
	// English: Fast path - check queue size before acquiring lock
	// 한글: Fast path - 락 획득 전 큐 크기 확인
	//       Send()의 release store를 따르는 acquire load로
	//       큐가 비어있다고 확인되면 삽입된 데이터를 반드시 볼 수 있음
	if (mSendQueueSize.load(std::memory_order_acquire) == 0)
	{
		// English: Queue is empty, release sending flag
		// 한글: 큐가 비어있음, 전송 플래그 해제
		mIsSending.store(false, std::memory_order_release);

		// English: [Fix D-3] TOCTOU guard: re-check queue size after releasing flag.
		//          Send() may have pushed data and lost the CAS race in the window
		//          between our size==0 check above and the store(false) above.
		//          If so, re-trigger FlushSendQueue() so the data is not stranded.
		// 한글: [Fix D-3] TOCTOU 방어: 플래그 해제 후 큐 크기 재확인.
		//       위의 size==0 확인과 store(false) 사이 구간에 Send()가 데이터를 추가하고
		//       CAS 경쟁에서 실패했을 수 있음. 그 경우 FlushSendQueue()를 재호출해
		//       데이터가 큐에 방치되지 않도록 한다.
		if (mSendQueueSize.load(std::memory_order_acquire) > 0)
		{
			FlushSendQueue();
		}
		return true;
	}

	std::vector<char> dataToSend;

	{
		std::lock_guard<std::mutex> lock(mSendMutex);

		// English: Double-check queue after acquiring lock (TOCTOU prevention)
		// 한글: Lock 획득 후 재확인 (TOCTOU 방지)
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
		mSendQueueSize.fetch_sub(1, std::memory_order_release);
	}

#ifdef _WIN32
	const SocketHandle socket = mSocket.load(std::memory_order_acquire);
	if (socket == GetInvalidSocket())
	{
		mIsSending.store(false, std::memory_order_release);
		return false;
	}

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
	int result = WSASend(socket, &mSendContext.wsaBuf, 1, &bytesSent, 0,
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
			//       오류 발생 시 먼저 atomic으로 플래그 해제
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
		mSocket.load(std::memory_order_acquire), dataToSend.data(), dataToSend.size(),
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

	const SocketHandle socket = mSocket.load(std::memory_order_acquire);
	if (socket == GetInvalidSocket())
	{
		return false;
	}

	mRecvContext.Reset();
	mRecvContext.wsaBuf.buf = mRecvContext.buffer;
	mRecvContext.wsaBuf.len = sizeof(mRecvContext.buffer);

	DWORD bytesReceived = 0;
	DWORD flags = 0;

	int result = WSARecv(socket, &mRecvContext.wsaBuf, 1, &bytesReceived,
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
	// English: POSIX platforms delegate recv to AsyncIOProvider::RecvAsync().
	//          PostRecv() is not used on this path — recv is driven by the
	//          platform engine (epoll/io_uring/kqueue) directly.
	// 한글: POSIX 플랫폼은 recv를 AsyncIOProvider::RecvAsync()에 위임.
	//       PostRecv()는 이 경로에서 사용되지 않으며 recv는
	//       플랫폼 엔진(epoll/io_uring/kqueue)이 직접 구동한다.
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
	std::vector<std::vector<char>> completePackets;
	bool shouldClose = false;

	{
		std::lock_guard<std::mutex> recvLock(mRecvMutex);

		// English: Overflow guard (slow-loris / flood defense).
		// 한글: 오버플로우 방어 (slow-loris / 플러드 방어).
		constexpr size_t kMaxAccumSize = MAX_PACKET_SIZE * 4;
		const size_t unread = mRecvAccumBuffer.size() - mRecvAccumOffset;
		if (unread + size > kMaxAccumSize)
		{
			Utils::Logger::Warn("Recv accumulation buffer overflow - Session: " +
							std::to_string(mId));
			mRecvAccumBuffer.clear();
			mRecvAccumOffset = 0;
			shouldClose = true;
		}
		else
		{
			mRecvAccumBuffer.insert(mRecvAccumBuffer.end(), data, data + size);

			while (mRecvAccumBuffer.size() - mRecvAccumOffset >= sizeof(PacketHeader))
			{
				const auto *hdr = reinterpret_cast<const PacketHeader *>(
					mRecvAccumBuffer.data() + mRecvAccumOffset);

				if (hdr->size < sizeof(PacketHeader) || hdr->size > MAX_PACKET_SIZE)
				{
					Utils::Logger::Warn("Invalid packet size " + std::to_string(hdr->size) +
								", resetting stream - Session: " + std::to_string(mId));
					mRecvAccumBuffer.clear();
					mRecvAccumOffset = 0;
					shouldClose = true;
					break;
				}

				if (mRecvAccumBuffer.size() - mRecvAccumOffset < hdr->size)
				{
					break;
				}

				const uint16_t packetSize = hdr->size;
				completePackets.emplace_back(
					mRecvAccumBuffer.begin() + static_cast<std::ptrdiff_t>(mRecvAccumOffset),
					mRecvAccumBuffer.begin() + static_cast<std::ptrdiff_t>(mRecvAccumOffset + packetSize));
				mRecvAccumOffset += packetSize;
			}

			if (mRecvAccumOffset >= mRecvAccumBuffer.size())
			{
				mRecvAccumBuffer.clear();
				mRecvAccumOffset = 0;
			}
			else if (mRecvAccumOffset > mRecvAccumBuffer.size() / 2)
			{
				mRecvAccumBuffer.erase(
					mRecvAccumBuffer.begin(),
					mRecvAccumBuffer.begin() + static_cast<std::ptrdiff_t>(mRecvAccumOffset));
				mRecvAccumOffset = 0;
			}
		}
	}

	if (shouldClose)
	{
		Close();
		return;
	}

	for (const auto &packet : completePackets)
	{
		OnRecv(packet.data(), static_cast<uint32_t>(packet.size()));
	}
}


} // namespace Network::Core
