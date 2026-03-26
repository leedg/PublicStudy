// Session 클래스 구현

#include "Session.h"
#include "SendBufferPool.h"
#include "SessionPool.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <sstream>
#if !defined(_WIN32)
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
		  mCurrentSendSlotIdx(~size_t(0)),
		  mRecvContext(IOType::Recv),
		  mSendContext(IOType::Send)
#endif
{
}

Session::~Session()
{
	Close();
}

#ifdef _WIN32
bool Session::TryResolveIOType(const OVERLAPPED *overlapped, IOType &outType)
{
	// SessionPool에 위임 — Initialize() 이후 불변 맵이므로 lock-free 읽기.
	return SessionPool::Instance().ResolveIOType(overlapped, outType);
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
	mIsSending.store(false, std::memory_order_release);
	mSendQueueSize.store(0, std::memory_order_relaxed);
	// mAsyncProvider is set separately via SetAsyncProvider()
#ifdef _WIN32
	mCurrentSendSlotIdx = ~size_t(0);
#endif
	mRecvAccumBuffer.clear();
	mRecvAccumOffset = 0;

	// 일반 recv 경로용 배치 버퍼 용량 사전 예약.
	// MAX_PACKET_SIZE * 4 = 최대 패킷 4개 분량을 한 번에 보관할 수 있는 크기.
	// 풀 세션 재사용 시 이미 capacity가 있으면 reserve() 재실행을 건너뛴다.
	if (mRecvBatchBuf.capacity() == 0)
	{
		mRecvBatchBuf.reserve(MAX_PACKET_SIZE * 4);
	}

	Utils::Logger::Info("Session initialized - ID: " + std::to_string(mId));
}

void Session::Reset()
{
	// 풀 재사용을 위한 경량 상태 초기화. Close() + WaitForPendingTasks() 이후에 호출.
	// mAsyncProvider는 Close()에서 정리. mRecvAccumBuffer는 여기서 초기화
	// (Close()에서 하지 않음) — 로직 워커 스레드의 ProcessRawRecv와의 race 방지.
	// 전체 이유는 Close() 주석 참고.
	mId = 0;
	// 여기서 송신 큐는 반드시 비어 있어야 함 — Close()가 mSendMutex 하에 드레인.
	// 비어 있지 않다면 Close()가 건너뛰어진 것으로, Close() → WaitForPendingTasks() → Reset() 계약 위반.
	assert(mSendQueue.empty() && "Reset() called without prior Close() — send queue not empty");
	mRecvAccumBuffer.clear();
	mRecvAccumOffset = 0;
	mState.store(SessionState::None, std::memory_order_relaxed);
	mPingSequence.store(0, std::memory_order_relaxed);
	mIsSending.store(false, std::memory_order_relaxed);
	mSendQueueSize.store(0, std::memory_order_relaxed);
	mOnRecvCb = nullptr;
	// 재사용 풀 슬롯이 새 태스크를 수락하도록 AsyncScope 초기화.
	// 안전: WaitForPendingTasks()로 모든 in-flight 람다 완료가 보장된 이후에만 호출됨.
	mAsyncScope.Reset();
#ifdef _WIN32
	mCurrentSendSlotIdx = ~size_t(0);
#endif
}

void Session::SetOnRecv(OnRecvCallback cb)
{
	mOnRecvCb = std::move(cb);
}

void Session::Close()
{
	// exchange로 이전 상태를 원자적으로 읽어 이중 닫기 경쟁(TOCTOU)을 방지.
	// compare_exchange 대신 exchange를 쓰는 이유: 중간 상태(Connecting, Disconnecting) 무관하게
	// "아직 Disconnected가 아니었으면 닫는다"는 단순 의미론이 충분하기 때문이다.
	SessionState prev = mState.exchange(SessionState::Disconnected, std::memory_order_acq_rel);
	if (prev == SessionState::Disconnected)
	{
		return;
	}

	// mAsyncProvider는 mSendMutex 내에서 초기화 (아래 큐 드레인 구간).
	// 상태가 이미 Disconnected이므로 concurrent Send()는 IsConnected()에서 먼저 종료된다.

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

	// closesocket() 이후 전송 중인 풀 슬롯 반납.
	// closesocket()이 대기 중인 WSASend를 강제 중단시켜 커널이 버퍼를
	// 더 이상 참조하지 않으므로 여기서 슬롯을 안전하게 반납할 수 있다.
#ifdef _WIN32
	if (mCurrentSendSlotIdx != ~size_t(0))
	{
		SendBufferPool::Instance().Release(mCurrentSendSlotIdx);
		mCurrentSendSlotIdx = ~size_t(0);
	}
#endif

	// 단일 락 내에서 async provider 초기화 및 송신 큐 드레인.
	{
		std::lock_guard<std::mutex> lock(mSendMutex);
		mAsyncProvider.reset();
#ifdef _WIN32
		// 큐 비우기 전 미전송 풀 슬롯 전체 반납.
		while (!mSendQueue.empty())
		{
			SendBufferPool::Instance().Release(mSendQueue.front().slotIdx);
			mSendQueue.pop();
		}
#else
		while (!mSendQueue.empty())
		{
			mSendQueue.pop();
		}
#endif
		mSendQueueSize.store(0, std::memory_order_relaxed);
	}
	// mRecvAccumBuffer는 여기서 초기화하지 않음.
	// 이유: PostSend() 실패 시 IOCP 워커 스레드가 Close()를 직접 호출
	// (KeyedDispatcher를 거치지 않음). 여기서 버퍼를 초기화하면
	// 로직 워커 스레드에서 동시에 실행 중인 ProcessRawRecv와 race 발생 가능.
	// 버퍼 초기화는 Reset()에서 수행하며, Reset()은 AsyncScope::WaitForDrain()으로
	// 모든 ProcessRawRecv 람다 완료가 보장된 이후에만 호출됨.

	// 큐잉된 로직 작업 취소. 이미 실행 중인 작업은 정상 완료;
	// 아직 디스패처 큐에 있는 작업은 조용히 건너뜀.
	// WaitForDrain()은 여기서 호출하지 않음 — 블로킹이며, Close()는
	// IOCP 워커 스레드에서 호출될 수 있음(송신 실패 경로).
	// 세션 재사용 전 모든 태스크 완료가 필요한 호출자는 Close() 후
	// WaitForPendingTasks()를 호출해야 함. 풀 세션은 SessionPool::ReleaseInternal이,
	// 비풀 세션은 ~AsyncScope()가 드레인을 담당함.
	mAsyncScope.Cancel();

	// 한 번도 할당되지 않은 사전예약 풀 슬롯은 로그 생략 (mId == 0).
	// 종료 시 SessionPool이 mSlots.reset()으로 슬롯을 파괴하고
	// ~Session() → Close()가 연쇄 호출됨. 전부 로그하면 실제 종료 로그가
	// 다수의 "Session closed - ID: 0"에 가려짐.
	if (mId != 0)
	{
		Utils::Logger::Info("Session closed - ID: " + std::to_string(mId));
	}
}

void Session::WaitForPendingTasks()
{
	// 모든 in-flight AsyncScope 태스크가 완료될 때까지 블로킹.
	// Cancel()은 Close()에서 호출되므로 아직 실행되지 않은 대기 태스크는
	// 빠르게 건너뜀. 실제로 실행 중인 태스크만 완료를 기다림.
	// 반환 후 Reset() 호출이 안전함.
	mAsyncScope.WaitForDrain(-1);
}

Session::SendResult Session::Send(const void *data, uint32_t size)
{
	// 인수를 먼저 검증.
	// null/크기0에 NotConnected를 반환하면 호출자가 활성 세션을 잘못 닫을 수 있으므로
	// 잘못된 입력에는 InvalidArgument, 상태 오류에는 NotConnected로 구분한다.
	if (data == nullptr || size == 0)
	{
		return SendResult::InvalidArgument;
	}
	if (!IsConnected())
	{
		return SendResult::NotConnected;
	}

	if (size > MAX_PACKET_TOTAL_SIZE)
	{
		Utils::Logger::Warn("Send size exceeds MAX_PACKET_TOTAL_SIZE - packet dropped (Session: " +
							std::to_string(mId) + ", Size: " + std::to_string(size) + ")");
		return SendResult::InvalidArgument;
	}

#ifdef _WIN32
	{
		const SocketHandle socket = mSocket.load(std::memory_order_acquire);
		if (socket == GetInvalidSocket())
		{
			return SendResult::NotConnected;
		}

		// mSendMutex 내에서 shared_ptr을 복사하고 락 해제 후 스냅샷을 사용.
		// Close()가 mAsyncProvider를 reset()할 때 발생하는 race를 방지한다.
		std::shared_ptr<AsyncIO::AsyncIOProvider> providerSnapshot;
		{
			std::lock_guard<std::mutex> lock(mSendMutex);
			providerSnapshot = mAsyncProvider;
		}

		if (providerSnapshot)
		{
			// RIO 경로 — 버퍼 관리를 RIOProvider가 직접 처리하므로 SendBufferPool을 거치지 않는다.
			auto error = providerSnapshot->SendAsync(
				socket, data, size, static_cast<AsyncIO::RequestContext>(mId));
			if (error != AsyncIO::AsyncIOError::Success)
			{
				Utils::Logger::Error(
					"RIO send failed - Session: " + std::to_string(mId) +
					", Error: " + std::string(providerSnapshot->GetLastError()));
				return SendResult::QueueFull;
			}
			else
			{
				(void)providerSnapshot->FlushRequests();
			}
			return SendResult::Ok;
		}
	}
#endif

	// 백프레셔: 송신 큐가 임계값을 초과하면 QueueFull 반환.
	// 묵시적 드롭 대신 명시적 피드백을 줘서 호출자가 흐름 제어를 직접 판단하게 한다.
	if (mSendQueueSize.load(std::memory_order_relaxed) >=
	    Utils::SEND_QUEUE_BACKPRESSURE_THRESHOLD)
	{
		Utils::Logger::Warn("Send backpressure triggered - Session: " +
							std::to_string(mId));
		return SendResult::QueueFull;
	}

#ifdef _WIN32
	// IOCP 경로 — 풀 슬롯 획득 (O(1), 힙 할당 없음) 후 데이터를 1회 복사.
	// SendBufferPool은 _aligned_malloc 연속 슬랩에서 O(1) 스택으로 슬롯을 대여한다.
	auto slot = SendBufferPool::Instance().Acquire();
	if (!slot.ptr)
	{
		Utils::Logger::Warn("SendBufferPool exhausted - packet dropped (Session: " +
							std::to_string(mId) + ")");
		return SendResult::QueueFull;
	}
	std::memcpy(slot.ptr, data, size);

	{
		std::lock_guard<std::mutex> lock(mSendMutex);
		mSendQueue.push({slot.index, size});
		mSendQueueSize.fetch_add(1, std::memory_order_release);
	}
#else
	// 비 IOCP 경로 — 힙 버퍼로 복사.
	std::vector<char> buffer(size);
	std::memcpy(buffer.data(), data, size);

	{
		std::lock_guard<std::mutex> lock(mSendMutex);
		mSendQueue.push(std::move(buffer));
		mSendQueueSize.fetch_add(1, std::memory_order_release);
	}
#endif

	// 항상 플러시 시도. FlushSendQueue 내부의 CAS가 동시 전송을 방지한다.
	FlushSendQueue();
	return SendResult::Ok;
}

void Session::FlushSendQueue()
{
	// compare_exchange_strong으로 mIsSending을 false→true로 원자적으로 전환.
	// 실패(이미 true)하면 다른 스레드가 PostSend 진행 중이므로 즉시 반환.
	// strong 변형을 쓰는 이유: 허위 실패(spurious failure)가 없어야 "정확히 한 스레드만
	// PostSend를 진행한다"는 보장이 성립하기 때문이다.
	bool expected = false;
	if (!mIsSending.compare_exchange_strong(expected, true))
	{
		return;
	}

	PostSend();
}

bool Session::PostSend()
{
	// Fast path — 락 획득 전 큐 크기 확인.
	// acquire 순서를 쓰는 이유: Send()에서 fetch_add(release)한 카운터를 여기서
	// 반드시 보이게 해야 큐가 비어있다는 잘못된 판단을 방지할 수 있다.
	if (mSendQueueSize.load(std::memory_order_acquire) == 0)
	{
		// 큐가 비어있으므로 전송 플래그 해제.
#ifdef _WIN32
		// 이전 전송 중 슬롯 반납 (방금 WSASend가 완료됐거나 아무것도 없는 상태).
		if (mCurrentSendSlotIdx != ~size_t(0))
		{
			SendBufferPool::Instance().Release(mCurrentSendSlotIdx);
			mCurrentSendSlotIdx = ~size_t(0);
		}
#endif

		mIsSending.store(false, std::memory_order_release);

		// TOCTOU 방어: mIsSending을 해제한 직후 다른 스레드가 Send()하여
		// 카운터를 올린 뒤 FlushSendQueue를 건너뛸 수 있다. 재확인하여 그 경우를 처리한다.
		if (mSendQueueSize.load(std::memory_order_acquire) > 0)
		{
			FlushSendQueue();
		}
		return true;
	}

#ifdef _WIN32
	SendRequest req{~size_t(0), 0};
#else
	std::vector<char> dataToSend;
#endif

	{
		std::lock_guard<std::mutex> lock(mSendMutex);

		// Lock 획득 후 재확인 (TOCTOU 방지): atomic 카운터가 0이 아니더라도
		// 실제 큐가 비어있을 수 있으므로 (fetch_sub 전 재확인) 락 내에서 한 번 더 검사.
		if (mSendQueue.empty())
		{
#ifdef _WIN32
			if (mCurrentSendSlotIdx != ~size_t(0))
			{
				SendBufferPool::Instance().Release(mCurrentSendSlotIdx);
				mCurrentSendSlotIdx = ~size_t(0);
			}
#endif
			mIsSending.store(false, std::memory_order_release);
			return true;
		}

#ifdef _WIN32
		req = mSendQueue.front();
		mSendQueue.pop();
#else
		dataToSend = std::move(mSendQueue.front());
		mSendQueue.pop();
#endif

		// release 순서로 큐 크기 감소: PostSend()의 다음 fast-path load(acquire)가
		// 이 감소를 반드시 관측하게 하여 빈 큐를 전송 대기 있음으로 오판하지 않게 한다.
		mSendQueueSize.fetch_sub(1, std::memory_order_release);
	}

#ifdef _WIN32
	// 다음 슬롯 커밋 전에 이전 전송 중 슬롯을 반납.
	// 이전 WSASend 완료 후 여기에 도달하므로 커널 참조가 없어 안전하다.
	if (mCurrentSendSlotIdx != ~size_t(0))
	{
		SendBufferPool::Instance().Release(mCurrentSendSlotIdx);
		mCurrentSendSlotIdx = ~size_t(0);
	}

	const SocketHandle socket = mSocket.load(std::memory_order_acquire);
	if (socket == GetInvalidSocket())
	{
		SendBufferPool::Instance().Release(req.slotIdx);
		mIsSending.store(false, std::memory_order_release);
		return false;
	}

	// Zero-copy: wsaBuf를 풀 슬롯 포인터에 직접 지정.
	// mSendContext.buffer는 Recv용 버퍼이므로 Send 경로에서 복사하지 않는다.
	mSendContext.Reset();
	mSendContext.wsaBuf.buf = SendBufferPool::Instance().SlotPtr(req.slotIdx);
	mSendContext.wsaBuf.len = static_cast<ULONG>(req.size);
	mCurrentSendSlotIdx = req.slotIdx;

	DWORD bytesSent = 0;
	int result = WSASend(socket, &mSendContext.wsaBuf, 1, &bytesSent, 0,
						 &mSendContext, nullptr);

	if (result == SOCKET_ERROR)
	{
		int error = WSAGetLastError();
		if (error != WSA_IO_PENDING)
		{
			Utils::Logger::Error("WSASend failed - Error: " + std::to_string(error));
			SendBufferPool::Instance().Release(req.slotIdx);
			mCurrentSendSlotIdx = ~size_t(0);
			mIsSending.store(false, std::memory_order_release);
			Close();
			return false;
		}
	}

	return true;
#else
	// POSIX 경로 — mSendMutex 내에서 provider 스냅샷 복사 후 락 해제 뒤 SendAsync() 호출.
	// 락을 보유한 채 SendAsync()를 호출하면 Close()와 교착 상태가 발생할 수 있다.
	std::shared_ptr<AsyncIO::AsyncIOProvider> providerSnapshot;
	{
		std::lock_guard<std::mutex> lock(mSendMutex);
		providerSnapshot = mAsyncProvider;
	}

	if (!providerSnapshot)
	{
		mIsSending.store(false, std::memory_order_release);
		return false;
	}

	auto sendError = providerSnapshot->SendAsync(
		mSocket.load(std::memory_order_acquire), dataToSend.data(), dataToSend.size(),
		static_cast<AsyncIO::RequestContext>(mId));

	if (sendError != AsyncIO::AsyncIOError::Success)
	{
		Utils::Logger::Error(
			"SendAsync failed - Session: " + std::to_string(mId) +
			", Error: " + std::string(providerSnapshot->GetLastError()));
		mIsSending.store(false, std::memory_order_release);
		Close();
		return false;
	}

	// mIsSending은 SendAsync()가 비동기로 완료될 때까지 true를 유지한다.
	// BaseNetworkEngine::ProcessSendCompletion()에서 PostSend()를 재호출하는 시점에 해제된다.
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
	// POSIX 플랫폼은 recv를 AsyncIOProvider::RecvAsync()에 위임한다.
	// epoll/io_uring/kqueue는 이벤트 루프 안에서 fd가 준비됐을 때 RecvAsync()를 직접 호출하므로
	// Session::PostRecv()는 POSIX 경로에서 사용되지 않는다.
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
	// Close()가 IOCP 송신 실패 경로에서 호출되기 전에 이미 큐잉된
	// in-flight recv 태스크가 실행될 수 있으므로 초반에 연결 상태를 확인한다.
	// mRecvAccumBuffer는 Close() 이후 접근 금지이며, Reset()(WaitForDrain 이후)에서만 초기화된다.
	if (!IsConnected())
	{
		return;
	}

	// PacketSpan은 평탄 배치 버퍼 내 offset+size만 기록하여 패킷별 alloc 없이 범위를 참조한다.
	struct PacketSpan { uint32_t offset; uint32_t size; };

	// 패스트패스 검사 — 누적 데이터 없고 수신 바이트가 정확히 1개의 완성 패킷인 경우.
	// KeyedDispatcher 친화도로 동일 세션 호출이 항상 같은 워커에서 직렬화되므로
	// 별도 mutex 없이도 mRecvAccumBuffer에 안전하게 접근할 수 있다.
	bool fastPath = false;
	if (mRecvAccumBuffer.empty() && size >= PACKET_HEADER_SIZE)
	{
		const auto *hdr = reinterpret_cast<const PacketHeader *>(data);
		if (hdr->size >= PACKET_HEADER_SIZE &&
		    hdr->size <= MAX_PACKET_TOTAL_SIZE &&
		    static_cast<uint32_t>(hdr->size) == size)
		{
			fastPath = true;
		}
	}

	if (fastPath)
	{
		// 할당 없는 패스트패스: 누적·복사 없이 원시 recv 버퍼를 그대로 전달.
		OnRecv(data, size);
		return;
	}

	// 일반 경로 — 완성 패킷을 mRecvBatchBuf(호출 간 재사용)에 배치 처리하고 swap 후 디스패치.
	// KeyedDispatcher 친화도로 직렬화가 보장되므로 별도 락은 불필요하다.
	std::vector<char>       localBatch;
	std::vector<PacketSpan> spans;
	bool shouldClose = false;

	{
		// 오버플로우 방어 (slow-loris / 플러드 방어).
		// MAX_PACKET_SIZE * 4 = 최대 패킷 4개 분량. 그 이상 누적되면 악의적 연결로 간주하고 종료.
		constexpr size_t kMaxAccumSize = MAX_PACKET_SIZE * 4;
		// 방어적 초기화: 파싱 루프가 offset <= size 불변식을 유지하지만,
		// 예상치 못한 회귀가 있을 경우 size_t underflow로 인한 OOB 읽기를 방지한다.
		if (mRecvAccumOffset > mRecvAccumBuffer.size())
			mRecvAccumOffset = 0;
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

			mRecvBatchBuf.clear(); // keep capacity

			while (mRecvAccumBuffer.size() - mRecvAccumOffset >= sizeof(PacketHeader))
			{
				const auto *hdr = reinterpret_cast<const PacketHeader *>(
					mRecvAccumBuffer.data() + mRecvAccumOffset);

				if (hdr->size < PACKET_HEADER_SIZE || hdr->size > MAX_PACKET_TOTAL_SIZE)
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

				const uint32_t packetSize = hdr->size;
				// 패킷 바이트를 재사용 배치 버퍼에 추가하고 오프셋+크기로 span 기록.
				spans.push_back({static_cast<uint32_t>(mRecvBatchBuf.size()), packetSize});
				mRecvBatchBuf.insert(mRecvBatchBuf.end(),
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

			// 배치를 localBatch로 이전한 후 mRecvBatchBuf에 즉시 capacity를 재확보.
			// swap()만 하면 mRecvBatchBuf.capacity()가 0으로 초기화되어 다음 호출 시
			// reserve()가 재실행되므로, swap 후 별도로 reserve()를 호출한다.
			mRecvBatchBuf.swap(localBatch);
			mRecvBatchBuf.reserve(MAX_PACKET_SIZE * 4);
		}
	}

	if (shouldClose)
	{
		Close();
		return;
	}

	// localBatch에서 완성 패킷을 순차 디스패치 (mRecvMutex 미보유 상태).
	for (const auto &sp : spans)
	{
		OnRecv(localBatch.data() + sp.offset, sp.size);
	}
}

} // namespace Network::Core
