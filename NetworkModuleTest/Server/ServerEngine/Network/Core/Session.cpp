// English: Session class implementation
// 한글: Session 클래스 구현

#include "Session.h"
#include "SendBufferPool.h"
#include "SessionPool.h"
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
	// English: Delegate to SessionPool — lock-free read from immutable map.
	// 한글: SessionPool에 위임 — 불변 맵에서 lock-free 읽기.
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
	mIsSending = false;
	mSendQueueSize.store(0, std::memory_order_relaxed);
	// mAsyncProvider is set separately via SetAsyncProvider()
#ifdef _WIN32
	mCurrentSendSlotIdx = ~size_t(0);
#endif
	mRecvAccumBuffer.clear();
	mRecvAccumOffset = 0;

	// English: Pre-reserve batch buffer capacity for the general recv path.
	// 한글: 일반 recv 경로용 배치 버퍼 용량 사전 예약.
	if (mRecvBatchBuf.capacity() == 0)
	{
		mRecvBatchBuf.reserve(MAX_PACKET_SIZE * 4);
	}

	Utils::Logger::Info("Session initialized - ID: " + std::to_string(mId));
}

void Session::Reset()
{
	// English: Lightweight state reset for pool reuse. Call after Close().
	//          mAsyncProvider and buffers are already cleaned by Close().
	// 한글: 풀 재사용을 위한 경량 상태 초기화. Close() 이후에 호출.
	//       mAsyncProvider·버퍼는 Close()에서 이미 정리됨.
	mId = 0;
	mState.store(SessionState::None, std::memory_order_relaxed);
	mPingSequence.store(0, std::memory_order_relaxed);
	mIsSending.store(false, std::memory_order_relaxed);
	mSendQueueSize.store(0, std::memory_order_relaxed);
	mOnRecvCb = nullptr;
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
	// English: Atomic exchange prevents TOCTOU double-close race
	// 한글: atomic exchange로 TOCTOU 이중 닫기 경쟁 방지
	SessionState prev = mState.exchange(SessionState::Disconnected, std::memory_order_acq_rel);
	if (prev == SessionState::Disconnected)
	{
		return;
	}

	// English: mAsyncProvider is reset inside mSendMutex (below, with queue drain).
	//          State is already Disconnected; any concurrent Send() will exit at
	//          IsConnected() before reaching the provider check.
	// 한글: mAsyncProvider는 mSendMutex 내에서 초기화 (아래 큐 드레인 구간).
	//       상태가 이미 Disconnected이므로 concurrent Send()는 IsConnected()에서 종료.

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

	// English: Release any in-flight send pool slot AFTER closesocket().
	//          closesocket() aborts the pending WSASend so the kernel no longer
	//          references the buffer — safe to return the slot here.
	// 한글: closesocket() 이후 전송 중인 풀 슬롯 반납.
	//       closesocket()이 대기 중인 WSASend를 중단시켜 커널이 버퍼를
	//       더 이상 참조하지 않으므로 여기서 슬롯을 안전하게 반납 가능.
#ifdef _WIN32
	if (mCurrentSendSlotIdx != ~size_t(0))
	{
		SendBufferPool::Instance().Release(mCurrentSendSlotIdx);
		mCurrentSendSlotIdx = ~size_t(0);
	}
#endif

	// English: Reset async provider and drain send queue under a single lock.
	// 한글: 단일 락 내에서 async provider 초기화 및 송신 큐 드레인.
	{
		std::lock_guard<std::mutex> lock(mSendMutex);
		mAsyncProvider.reset();
#ifdef _WIN32
		// English: Return all queued (unsent) pool slots before draining the queue.
		// 한글: 큐 비우기 전 미전송 풀 슬롯 전체 반납.
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

	if (size > MAX_PACKET_TOTAL_SIZE)
	{
		Utils::Logger::Warn("Send size exceeds MAX_PACKET_TOTAL_SIZE - packet dropped (Session: " +
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

		// English: Copy shared_ptr under mSendMutex, then use snapshot outside lock.
		//          This prevents a race with Close() resetting mAsyncProvider.
		// 한글: mSendMutex 내에서 shared_ptr을 복사하고 락 해제 후 스냅샷 사용.
		//       Close()가 mAsyncProvider를 초기화하는 race를 방지한다.
		std::shared_ptr<AsyncIO::AsyncIOProvider> providerSnapshot;
		{
			std::lock_guard<std::mutex> lock(mSendMutex);
			providerSnapshot = mAsyncProvider;
		}

		if (providerSnapshot)
		{
			// English: RIO path - delegate directly to async provider
			// 한글: RIO 경로 - 비동기 공급자에 직접 위임
			auto error = providerSnapshot->SendAsync(
				socket, data, size, static_cast<AsyncIO::RequestContext>(mId));
			if (error != AsyncIO::AsyncIOError::Success)
			{
				Utils::Logger::Error(
					"RIO send failed - Session: " + std::to_string(mId) +
					", Error: " + std::string(providerSnapshot->GetLastError()));
			}
			else
			{
				(void)providerSnapshot->FlushRequests();
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

#ifdef _WIN32
	// English: IOCP path — acquire a pool slot (O(1), no heap alloc) and copy once.
	// 한글: IOCP 경로 — 풀 슬롯 획득 (O(1), 힙 할당 없음) 후 1회 복사.
	auto slot = SendBufferPool::Instance().Acquire();
	if (!slot.ptr)
	{
		Utils::Logger::Warn("SendBufferPool exhausted - packet dropped (Session: " +
							std::to_string(mId) + ")");
		return;
	}
	std::memcpy(slot.ptr, data, size);

	{
		std::lock_guard<std::mutex> lock(mSendMutex);
		mSendQueue.push({slot.index, size});
		mSendQueueSize.fetch_add(1, std::memory_order_release);
	}
#else
	// English: Non-IOCP path — copy into a heap buffer (existing behaviour).
	// 한글: 비 IOCP 경로 — 힙 버퍼로 복사 (기존 동작).
	std::vector<char> buffer(size);
	std::memcpy(buffer.data(), data, size);

	{
		std::lock_guard<std::mutex> lock(mSendMutex);
		mSendQueue.push(std::move(buffer));
		mSendQueueSize.fetch_add(1, std::memory_order_release);
	}
#endif

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
	if (mSendQueueSize.load(std::memory_order_acquire) == 0)
	{
		// English: Queue is empty, release sending flag
		// 한글: 큐가 비어있음, 전송 플래그 해제

#ifdef _WIN32
		// English: Release the previous in-flight slot (send just completed).
		// 한글: 이전 전송 중 슬롯 반납 (방금 전송 완료).
		if (mCurrentSendSlotIdx != ~size_t(0))
		{
			SendBufferPool::Instance().Release(mCurrentSendSlotIdx);
			mCurrentSendSlotIdx = ~size_t(0);
		}
#endif

		mIsSending.store(false, std::memory_order_release);

		// English: [Fix D-3] TOCTOU guard: re-check queue size after releasing flag.
		// 한글: [Fix D-3] TOCTOU 방어: 플래그 해제 후 큐 크기 재확인.
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

		// English: Double-check queue after acquiring lock (TOCTOU prevention)
		// 한글: Lock 획득 후 재확인 (TOCTOU 방지)
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

		// English: Decrement queue size atomically
		// 한글: Atomic으로 큐 크기 감소
		mSendQueueSize.fetch_sub(1, std::memory_order_release);
	}

#ifdef _WIN32
	// English: Release the previous in-flight slot before committing the next one.
	// 한글: 다음 슬롯 커밋 전에 이전 전송 중 슬롯 반납.
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

	// English: Zero-copy: point wsaBuf directly at the pool slot (no memcpy into mSendContext.buffer).
	// 한글: Zero-copy: wsaBuf를 풀 슬롯에 직접 지정 (mSendContext.buffer로의 memcpy 없음).
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
	// English: POSIX path — copy provider snapshot under mSendMutex to avoid race with Close().
	// 한글: POSIX 경로 — Close()와의 race 방지를 위해 mSendMutex 내에서 provider 스냅샷 복사.
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
	// English: PacketSpan records offset+size within the flat batch buffer (no per-packet alloc).
	// 한글: PacketSpan은 평탄 배치 버퍼 내 offset+size만 기록 (패킷별 alloc 없음).
	struct PacketSpan { uint32_t offset; uint32_t size; };

	// English: Fast-path check — no accumulated data and exactly one complete packet.
	//          Holds the lock only during the check; releases before invoking OnRecv.
	// 한글: 패스트패스 검사 — 누적 데이터 없고 정확히 1개의 완성 패킷인 경우.
	//       검사 중에만 락 보유; OnRecv 호출 전에 해제.
	bool fastPath = false;
	{
		std::lock_guard<std::mutex> recvLock(mRecvMutex);
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
	}

	if (fastPath)
	{
		// English: Zero-alloc fast path: deliver raw recv buffer directly.
		// 한글: 할당 없는 패스트패스: 원시 recv 버퍼를 직접 전달.
		OnRecv(data, size);
		return;
	}

	// English: General path — batch complete packets into mRecvBatchBuf (reused across calls),
	//          swap with a local variable to dispatch outside mRecvMutex.
	// 한글: 일반 경로 — 완성 패킷을 mRecvBatchBuf(호출 간 재사용)에 배치 처리하고,
	//       지역 변수와 swap하여 mRecvMutex 밖에서 디스패치.
	std::vector<char>       localBatch;
	std::vector<PacketSpan> spans;
	bool shouldClose = false;

	{
		std::lock_guard<std::mutex> recvLock(mRecvMutex);

		// English: Overflow guard (slow-loris / flood defense).
		// 한글: 오버플로우 방어 (slow-loris / 플러드 방어).
		constexpr size_t kMaxAccumSize = MAX_PACKET_SIZE * 4;
		// English: Defensive reset in case of internal regression — the invariant
		//          (offset <= size) is maintained by the parsing loop below, but
		//          resetting here prevents size_t underflow from causing OOB reads.
		// 한글: 내부 회귀 대비 방어 초기화 — 아래 파싱 루프가 불변식을 유지하지만
		//       여기서 리셋하면 size_t underflow로 인한 OOB 읽기를 방지한다.
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
				// English: Append packet bytes to reusable batch buffer and record its span.
				// 한글: 패킷 바이트를 재사용 배치 버퍼에 추가하고 span 기록.
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

			// English: Transfer ownership to localBatch for dispatch outside the lock.
			// 한글: 락 밖에서 디스패치하기 위해 localBatch로 소유권 이전.
			mRecvBatchBuf.swap(localBatch);
		}
	}

	if (shouldClose)
	{
		Close();
		return;
	}

	// English: Dispatch all packets from localBatch (mRecvMutex not held).
	// 한글: localBatch에서 모든 패킷 디스패치 (mRecvMutex 미보유 상태).
	for (const auto &sp : spans)
	{
		OnRecv(localBatch.data() + sp.offset, sp.size);
	}
}


} // namespace Network::Core
