// Session class implementation

#include "Session.h"
#include "SendBufferPool.h"
#include "SessionPool.h"
#include <cassert>
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
	// Delegate to SessionPool — lock-free read from immutable map.
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

	// Pre-reserve batch buffer capacity for the general recv path.
	if (mRecvBatchBuf.capacity() == 0)
	{
		mRecvBatchBuf.reserve(MAX_PACKET_SIZE * 4);
	}

	Utils::Logger::Info("Session initialized - ID: " + std::to_string(mId));
}

void Session::Reset()
{
	// Lightweight state reset for pool reuse. Call after Close() + WaitForDrain().
	//          mAsyncProvider is cleaned in Close(). mRecvAccumBuffer is cleared here
	//          (not in Close()) to avoid racing with ProcessRawRecv on a logic worker
	//          thread — see Close() comment for full rationale.
	mId = 0;
	// Send queue MUST be empty here — Close() drains it under mSendMutex.
	//          A non-empty queue at this point indicates Close() was skipped, which
	//          violates the Close() → WaitForPendingTasks() → Reset() contract.
	assert(mSendQueue.empty() && "Reset() called without prior Close() — send queue not empty");
	mRecvAccumBuffer.clear();
	mRecvAccumOffset = 0;
	mState.store(SessionState::None, std::memory_order_relaxed);
	mPingSequence.store(0, std::memory_order_relaxed);
	mIsSending.store(false, std::memory_order_relaxed);
	mSendQueueSize.store(0, std::memory_order_relaxed);
	mOnRecvCb = nullptr;
	// Reset AsyncScope so reused pool slots accept new tasks.
	//          Safe here: all in-flight lambdas held sessionCopy refs and have
	//          already completed before ReleaseInternal drops the last ref.
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
	// Atomic exchange prevents TOCTOU double-close race
	SessionState prev = mState.exchange(SessionState::Disconnected, std::memory_order_acq_rel);
	if (prev == SessionState::Disconnected)
	{
		return;
	}

	// mAsyncProvider is reset inside mSendMutex (below, with queue drain).
	//          State is already Disconnected; any concurrent Send() will exit at
	//          IsConnected() before reaching the provider check.

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

	// Release any in-flight send pool slot AFTER closesocket().
	//          closesocket() aborts the pending WSASend so the kernel no longer
	//          references the buffer — safe to return the slot here.
#ifdef _WIN32
	if (mCurrentSendSlotIdx != ~size_t(0))
	{
		SendBufferPool::Instance().Release(mCurrentSendSlotIdx);
		mCurrentSendSlotIdx = ~size_t(0);
	}
#endif

	// Reset async provider and drain send queue under a single lock.
	{
		std::lock_guard<std::mutex> lock(mSendMutex);
		mAsyncProvider.reset();
#ifdef _WIN32
		// Return all queued (unsent) pool slots before draining the queue.
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
	// mRecvAccumBuffer is NOT cleared here.
	//          Rationale: PostSend() failure on an IOCP worker thread calls Close()
	//          directly (not routed through KeyedDispatcher), so clearing the buffer
	//          here could race with ProcessRawRecv running concurrently on a logic
	//          worker thread. The buffer is cleared in Reset(), which is called only
	//          after AsyncScope::WaitForDrain() ensures all in-flight ProcessRawRecv
	//          lambdas have completed.

	// Cancel queued logic tasks. Tasks already running will finish normally;
	//          tasks still in the dispatcher queue will be silently skipped.
	//          WaitForDrain() is NOT called here — it blocks, and Close() can be called
	//          from an IOCP worker thread (send-failure path). Callers that need to
	//          ensure all tasks are done before recycling the session must call
	//          WaitForPendingTasks() after Close(). For pool sessions this is done by
	//          SessionPool::ReleaseInternal. For non-pool sessions ~AsyncScope() drains.
	mAsyncScope.Cancel();

	Utils::Logger::Info("Session closed - ID: " + std::to_string(mId));
}

void Session::WaitForPendingTasks()
{
	// Block until all in-flight AsyncScope tasks have completed.
	//          Cancel() is called by Close(), so pending-but-not-yet-running tasks will
	//          be skipped quickly. Only truly in-flight (currently executing) tasks
	//          need to finish. After this returns, Reset() is safe to call.
	mAsyncScope.WaitForDrain(-1);
}

Session::SendResult Session::Send(const void *data, uint32_t size)
{
	if (!IsConnected() || data == nullptr || size == 0)
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

		// Copy shared_ptr under mSendMutex, then use snapshot outside lock.
		//          This prevents a race with Close() resetting mAsyncProvider.
		std::shared_ptr<AsyncIO::AsyncIOProvider> providerSnapshot;
		{
			std::lock_guard<std::mutex> lock(mSendMutex);
			providerSnapshot = mAsyncProvider;
		}

		if (providerSnapshot)
		{
			// RIO path - delegate directly to async provider
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

	// Back-pressure: return QueueFull if send queue exceeds threshold.
	//          Caller receives explicit feedback instead of a silent drop.
	if (mSendQueueSize.load(std::memory_order_relaxed) >=
	    Utils::SEND_QUEUE_BACKPRESSURE_THRESHOLD)
	{
		Utils::Logger::Warn("Send backpressure triggered - Session: " +
							std::to_string(mId));
		return SendResult::QueueFull;
	}

	// Lock contention optimization using atomic queue size counter

#ifdef _WIN32
	// IOCP path — acquire a pool slot (O(1), no heap alloc) and copy once.
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
	// Non-IOCP path — copy into a heap buffer (existing behaviour).
	std::vector<char> buffer(size);
	std::memcpy(buffer.data(), data, size);

	{
		std::lock_guard<std::mutex> lock(mSendMutex);
		mSendQueue.push(std::move(buffer));
		mSendQueueSize.fetch_add(1, std::memory_order_release);
	}
#endif

	// Always try to flush (CAS inside will prevent double send)
	FlushSendQueue();
	return SendResult::Ok;
}

void Session::FlushSendQueue()
{
	// CAS to prevent concurrent sends
	bool expected = false;
	if (!mIsSending.compare_exchange_strong(expected, true))
	{
		return;
	}

	PostSend();
}

bool Session::PostSend()
{
	// Fast path - check queue size before acquiring lock
	if (mSendQueueSize.load(std::memory_order_acquire) == 0)
	{
		// Queue is empty, release sending flag

#ifdef _WIN32
		// Release the previous in-flight slot (send just completed).
		if (mCurrentSendSlotIdx != ~size_t(0))
		{
			SendBufferPool::Instance().Release(mCurrentSendSlotIdx);
			mCurrentSendSlotIdx = ~size_t(0);
		}
#endif

		mIsSending.store(false, std::memory_order_release);

		// [Fix D-3] TOCTOU guard: re-check queue size after releasing flag.
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

		// Double-check queue after acquiring lock (TOCTOU prevention)
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

		// Decrement queue size atomically
		mSendQueueSize.fetch_sub(1, std::memory_order_release);
	}

#ifdef _WIN32
	// Release the previous in-flight slot before committing the next one.
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

	// Zero-copy: point wsaBuf directly at the pool slot (no memcpy into mSendContext.buffer).
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
	// POSIX path — copy provider snapshot under mSendMutex to avoid race with Close().
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

	// mIsSending stays true until send completion fires ProcessSendCompletion.
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
	// POSIX platforms delegate recv to AsyncIOProvider::RecvAsync().
	//          PostRecv() is not used on this path — recv is driven by the
	//          platform engine (epoll/io_uring/kqueue) directly.
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
	// Guard against in-flight recv tasks that were queued before Close() fired
	//          from the IOCP send-failure path. mRecvAccumBuffer must not be accessed
	//          after Close() — it is cleared only in Reset() (post-WaitForDrain).
	if (!IsConnected())
	{
		return;
	}

	// PacketSpan records offset+size within the flat batch buffer (no per-packet alloc).
	struct PacketSpan { uint32_t offset; uint32_t size; };

	// Fast-path check — no accumulated data and exactly one complete packet.
	//          No lock needed: KeyedDispatcher guarantees this method is only called
	//          from the session's dedicated worker thread.
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
		// Zero-alloc fast path: deliver raw recv buffer directly.
		OnRecv(data, size);
		return;
	}

	// General path — batch complete packets into mRecvBatchBuf (reused across calls),
	//          swap with a local variable before dispatching.
	//          No lock needed: serialization is guaranteed by KeyedDispatcher affinity.
	std::vector<char>       localBatch;
	std::vector<PacketSpan> spans;
	bool shouldClose = false;

	{

		// Overflow guard (slow-loris / flood defense).
		constexpr size_t kMaxAccumSize = MAX_PACKET_SIZE * 4;
		// Defensive reset in case of internal regression — the invariant
		//          (offset <= size) is maintained by the parsing loop below, but
		//          resetting here prevents size_t underflow from causing OOB reads.
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
				// Append packet bytes to reusable batch buffer and record its span.
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

			// Transfer batch to localBatch, then immediately re-arm mRecvBatchBuf
			//          with pre-reserved capacity so the next call skips the reserve() call.
			//          swap() alone resets mRecvBatchBuf.capacity() to 0 (defeats the intent).
			mRecvBatchBuf.swap(localBatch);
			mRecvBatchBuf.reserve(MAX_PACKET_SIZE * 4);
		}
	}

	if (shouldClose)
	{
		Close();
		return;
	}

	// Dispatch all packets from localBatch (mRecvMutex not held).
	for (const auto &sp : spans)
	{
		OnRecv(localBatch.data() + sp.offset, sp.size);
	}
}

} // namespace Network::Core
