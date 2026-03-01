// English: io_uring-based AsyncIOProvider implementation
//          Compiled only when HAVE_LIBURING is defined (CMake find_library check).
// 한글: io_uring 기반 AsyncIOProvider 구현
//       HAVE_LIBURING이 정의된 경우에만 컴파일 (CMake find_library 검사).

#if defined(__linux__) && (defined(HAVE_IO_URING) || defined(HAVE_LIBURING))

#include "IOUringAsyncIOProvider.h"
#include "PlatformDetect.h"
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace Network
{
namespace AsyncIO
{
namespace Linux
{
// =============================================================================
// English: Constructor & Destructor
// 한글: 생성자 및 소멸자
// =============================================================================

IOUringAsyncIOProvider::IOUringAsyncIOProvider()
	: mInfo{}, mStats{}, mMaxConcurrentOps(0), mNextBufferId(1), mNextOpKey(1),
		  mInitialized(false), mSupportsFixedBuffers(false),
		  mSupportsDirectDescriptors(false)
{
	std::memset(&mRing, 0, sizeof(io_uring));
}

IOUringAsyncIOProvider::~IOUringAsyncIOProvider() { Shutdown(); }

// =============================================================================
// English: Lifecycle Management
// 한글: 생명주기 관리
// =============================================================================

AsyncIOError IOUringAsyncIOProvider::Initialize(size_t queueDepth,
												size_t maxConcurrent)
{
	if (mInitialized)
		return AsyncIOError::AlreadyInitialized;

	mMaxConcurrentOps = maxConcurrent;

	// English: Initialize io_uring ring with specified queue depth
	// 한글: 지정된 큐 깊이로 io_uring 링 초기화
	struct io_uring_params params;
	std::memset(&params, 0, sizeof(params));

	// English: Cap queue depth at 4096 (io_uring limit)
	// 한글: 큐 깊이를 4096으로 제한 (io_uring 제한)
	size_t actualDepth = (queueDepth > 4096) ? 4096 : queueDepth;

	int ret = io_uring_queue_init_params(static_cast<unsigned>(actualDepth),
										 &mRing, &params);
	if (ret < 0)
	{
		mLastError = "io_uring_queue_init_params failed";
		return AsyncIOError::OperationFailed;
	}

	// English: Check feature support
	// 한글: 기능 지원 확인
	unsigned int features = mRing.features;
	mSupportsDirectDescriptors = (features & IORING_FEAT_NODROP) != 0;

	// English: Initialize recv pool with fixed-buffer mode; fall back to
	//          non-fixed if io_uring_register_buffers is unsupported.
	// 한글: recv 풀을 고정 버퍼 모드로 초기화; 커널 미지원 시 일반 모드로 폴백.
	constexpr size_t kSlotSize = 8192;
	if (!mRecvPool.InitializeFixed(&mRing, maxConcurrent, kSlotSize))
	{
		if (!mRecvPool.Initialize(maxConcurrent, kSlotSize))
		{
			io_uring_queue_exit(&mRing);
			mLastError = "Failed to initialize recv buffer pool";
			return AsyncIOError::AllocationFailed;
		}
	}

	// English: Initialize send pool (no kernel registration needed for sends).
	// 한글: 송신 풀 초기화 (커널 등록 불필요).
	if (!mSendPool.Initialize(maxConcurrent, kSlotSize))
	{
		mRecvPool.Shutdown();
		io_uring_queue_exit(&mRing);
		mLastError = "Failed to initialize send buffer pool";
		return AsyncIOError::AllocationFailed;
	}

	mSupportsFixedBuffers = mRecvPool.IsFixedBufferMode();

	// English: Initialize provider info
	// 한글: 공급자 정보 초기화
	mInfo.mPlatformType = PlatformType::IOUring;
	mInfo.mName = "io_uring";
	mInfo.mMaxQueueDepth = actualDepth;
	mInfo.mMaxConcurrentReq = maxConcurrent;
	mInfo.mSupportsBufferReg = mSupportsFixedBuffers;
	mInfo.mSupportsBatching = true;
	mInfo.mSupportsZeroCopy = mSupportsFixedBuffers;

	mInitialized = true;
	return AsyncIOError::Success;
}

void IOUringAsyncIOProvider::Shutdown()
{
	if (!mInitialized)
		return;

	std::lock_guard<std::mutex> lock(mMutex);

	mRegisteredBuffers.clear();
	mPendingOps.clear();

	// English: Shutdown pools before ring exit.
	//          IOUringBufferPool::Shutdown() calls io_uring_unregister_buffers()
	//          which must happen while the ring is still alive.
	// 한글: 링 종료 전 풀 종료.
	//       IOUringBufferPool::Shutdown()이 io_uring_unregister_buffers()를 호출하므로
	//       링이 살아있는 동안 먼저 호출해야 한다.
	mRecvPool.Shutdown();
	mSendPool.Shutdown();

	// English: Exit the ring
	// 한글: 링 종료
	io_uring_queue_exit(&mRing);
	mInitialized = false;
}

bool IOUringAsyncIOProvider::IsInitialized() const { return mInitialized; }

// =============================================================================
// English: Socket Association
// 한글: 소켓 연결
// =============================================================================

AsyncIOError IOUringAsyncIOProvider::AssociateSocket(SocketHandle socket,
													 RequestContext context)
{
	// English: io_uring doesn't require explicit socket association
	// 한글: io_uring은 명시적 소켓 연결이 필요하지 않음
	// io_uring operates on file descriptors directly via SQE submissions,
	// no prior registration needed (unlike IOCP/epoll).
	if (!mInitialized)
		return AsyncIOError::NotInitialized;

	return AsyncIOError::Success;
}

// =============================================================================
// English: Buffer Management
// 한글: 버퍼 관리
// =============================================================================

int64_t IOUringAsyncIOProvider::RegisterBuffer(const void *ptr, size_t size)
{
	if (!mInitialized || !ptr || size == 0)
		return -1;

	std::lock_guard<std::mutex> lock(mMutex);

	// English: Store buffer registration (simple mapping)
	// 한글: 버퍼 등록 저장 (단순 매핑)
	int64_t bufferId = mNextBufferId++;
	RegisteredBufferEntry entry;
	entry.mAddress = const_cast<void *>(ptr);
	entry.mSize = static_cast<uint32_t>(size);
	entry.mBufferGroupId = static_cast<int32_t>(bufferId);

	mRegisteredBuffers[bufferId] = entry;
	return bufferId;
}

AsyncIOError IOUringAsyncIOProvider::UnregisterBuffer(int64_t bufferId)
{
	if (!mInitialized)
		return AsyncIOError::NotInitialized;

	std::lock_guard<std::mutex> lock(mMutex);

	auto it = mRegisteredBuffers.find(bufferId);
	if (it == mRegisteredBuffers.end())
		return AsyncIOError::InvalidBuffer;

	mRegisteredBuffers.erase(it);
	return AsyncIOError::Success;
}

// =============================================================================
// English: Async I/O Operations
// 한글: 비동기 I/O 작업
// =============================================================================

AsyncIOError IOUringAsyncIOProvider::SendAsync(SocketHandle socket,
												   const void *buffer, size_t size,
												   RequestContext context,
												   uint32_t flags)
{
	if (!mInitialized)
		return AsyncIOError::NotInitialized;
	if (socket < 0 || !buffer || size == 0)
		return AsyncIOError::InvalidParameter;

	// English: Acquire send pool slot before taking the main lock.
	// 한글: 메인 락 취득 전 송신 풀 슬롯 획득.
	Core::Memory::BufferSlot sendSlot = mSendPool.Acquire();
	if (!sendSlot.ptr)
		return AsyncIOError::NoResources;

	std::memcpy(sendSlot.ptr, buffer, size);

	std::lock_guard<std::mutex> lock(mMutex);

	// English: Store pending operation
	// 한글: 대기 작업 저장
	uint64_t opKey = mNextOpKey++;
	PendingOperation pending;
	pending.mContext        = context;
	pending.mType           = AsyncIOType::Send;
	pending.mSocket         = socket;
	pending.mCallerBuffer   = nullptr;         // not needed for send
	pending.mPoolSlotPtr    = sendSlot.ptr;
	pending.mBufferSize     = static_cast<uint32_t>(size);
	pending.mPoolSlotIndex  = sendSlot.index;

	mPendingOps[opKey] = std::move(pending);

	// English: Prepare send operation in io_uring SQ
	// 한글: io_uring SQ에 송신 작업 준비
	struct io_uring_sqe *sqe = io_uring_get_sqe(&mRing);
	if (!sqe)
	{
		mLastError = "io_uring SQ full";
		mSendPool.Release(sendSlot.index);
		mPendingOps.erase(opKey);
		return AsyncIOError::NoResources;
	}

	io_uring_prep_send(sqe, socket, sendSlot.ptr, size, 0);
	sqe->user_data = opKey;

	mStats.mTotalRequests++;
	mStats.mPendingRequests++;

	// English: Submit to ring. On failure roll back the pending op and pool slot
	//          so the caller can retry. The prepped SQE remains in the SQ but
	//          has no matching pending entry — a stale CQE for this opKey will
	//          be silently ignored in ProcessCompletionQueue().
	// 한글: 링 제출. 실패 시 pending op와 풀 슬롯을 롤백하여 호출자가 재시도 가능.
	//       prep된 SQE는 SQ에 남지만 매칭 항목이 없어 ProcessCompletionQueue()에서
	//       무시된다.
	if (!SubmitRing())
	{
		mSendPool.Release(sendSlot.index);
		mPendingOps.erase(opKey);
		mStats.mTotalRequests--;
		mStats.mPendingRequests--;
		return AsyncIOError::OperationFailed;
	}
	return AsyncIOError::Success;
}

AsyncIOError IOUringAsyncIOProvider::RecvAsync(SocketHandle socket,
												   void *buffer, size_t size,
												   RequestContext context,
												   uint32_t flags)
{
	if (!mInitialized)
		return AsyncIOError::NotInitialized;
	if (socket < 0 || !buffer || size == 0)
		return AsyncIOError::InvalidParameter;

	// English: Acquire recv pool slot before taking the main lock.
	// 한글: 메인 락 취득 전 수신 풀 슬롯 획득.
	Core::Memory::BufferSlot recvSlot = mRecvPool.Acquire();
	if (!recvSlot.ptr)
		return AsyncIOError::NoResources;

	std::lock_guard<std::mutex> lock(mMutex);

	// English: Store pending operation
	// 한글: 대기 작업 저장
	uint64_t opKey = mNextOpKey++;
	PendingOperation pending;
	pending.mContext        = context;
	pending.mType           = AsyncIOType::Recv;
	pending.mSocket         = socket;
	pending.mCallerBuffer   = buffer;           // destination for completed data
	pending.mPoolSlotPtr    = recvSlot.ptr;
	pending.mBufferSize     = static_cast<uint32_t>(size);
	pending.mPoolSlotIndex  = recvSlot.index;

	mPendingOps[opKey] = std::move(pending);

	// English: Prepare receive operation.
	//          Use fixed-buffer read when pool is registered with the ring
	//          (zero-copy kernel path); fall back to regular recv otherwise.
	// 한글: 수신 작업 준비.
	//       풀이 링에 등록된 경우 fixed-buffer read 사용 (zero-copy 커널 경로);
	//       미지원 시 일반 recv 폴백.
	struct io_uring_sqe *sqe = io_uring_get_sqe(&mRing);
	if (!sqe)
	{
		mLastError = "io_uring SQ full";
		mRecvPool.Release(recvSlot.index);
		mPendingOps.erase(opKey);
		return AsyncIOError::NoResources;
	}

	if (mRecvPool.IsFixedBufferMode())
	{
		int bufIdx = mRecvPool.GetFixedBufferIndex(recvSlot.index);
		io_uring_prep_read_fixed(sqe, socket, recvSlot.ptr,
								 static_cast<unsigned>(size), 0, bufIdx);
	}
	else
	{
		io_uring_prep_recv(sqe, socket, recvSlot.ptr, size, 0);
	}
	sqe->user_data = opKey;

	mStats.mTotalRequests++;
	mStats.mPendingRequests++;

	// English: Submit to ring. On failure roll back the pending op and pool slot.
	// 한글: 링 제출. 실패 시 pending op와 풀 슬롯 롤백.
	if (!SubmitRing())
	{
		mRecvPool.Release(recvSlot.index);
		mPendingOps.erase(opKey);
		mStats.mTotalRequests--;
		mStats.mPendingRequests--;
		return AsyncIOError::OperationFailed;
	}
	return AsyncIOError::Success;
}

AsyncIOError IOUringAsyncIOProvider::FlushRequests()
{
	// English: Submit all SQ entries to kernel
	// 한글: 모든 SQ 항목을 커널에 제출
	if (!mInitialized)
		return AsyncIOError::NotInitialized;

	return SubmitRing() ? AsyncIOError::Success : AsyncIOError::OperationFailed;
}

// =============================================================================
// English: Completion Processing
// 한글: 완료 처리
// =============================================================================

int IOUringAsyncIOProvider::ProcessCompletions(CompletionEntry *entries,
												   size_t maxEntries, int timeoutMs)
{
	if (!mInitialized)
		return static_cast<int>(AsyncIOError::NotInitialized);
	if (!entries || maxEntries == 0)
		return static_cast<int>(AsyncIOError::InvalidParameter);

	std::lock_guard<std::mutex> lock(mMutex);

	// English: Process available completions
	// 한글: 사용 가능한 완료 처리
	int count = ProcessCompletionQueue(entries, maxEntries);

	// English: If no completions and timeout > 0, wait
	// 한글: 완료 없고 타임아웃 > 0이면 대기
	if (count == 0 && timeoutMs != 0)
	{
		struct __kernel_timespec ts;
		ts.tv_sec = (timeoutMs > 0) ? (timeoutMs / 1000) : 0;
		ts.tv_nsec = (timeoutMs > 0) ? ((timeoutMs % 1000) * 1000000) : 0;

		struct io_uring_cqe *cqe;
		int ret = io_uring_wait_cqe_timeout(&mRing, &cqe,
											(timeoutMs > 0) ? &ts : nullptr);
		if (ret == 0)
		{
			count = ProcessCompletionQueue(entries, maxEntries);
		}
	}

	return count;
}

int IOUringAsyncIOProvider::ProcessCompletionQueue(CompletionEntry *entries,
													   size_t maxEntries)
{
	int processedCount = 0;
	unsigned head;
	struct io_uring_cqe *cqe;

	io_uring_for_each_cqe(&mRing, head, cqe)
	{
		if (static_cast<size_t>(processedCount) >= maxEntries)
			break;

		uint64_t opKey = cqe->user_data;
		int res = cqe->res;

		auto it = mPendingOps.find(opKey);
		if (it != mPendingOps.end())
		{
			const PendingOperation &op = it->second;

			// English: Fill completion entry
			// 한글: 완료 항목 채우기
			CompletionEntry &entry = entries[processedCount];
			entry.mContext        = op.mContext;
			entry.mType           = op.mType;
			entry.mResult         = static_cast<int32_t>(res);
			entry.mOsError        = (res < 0) ? static_cast<OSError>(-res) : 0;
			entry.mCompletionTime = 0;

			// English: For recv completions copy data from the pool slot to the
			//          caller's buffer, then release the slot back to the pool.
			//          For send completions just release the send slot.
			// 한글: recv 완료 시 풀 슬롯에서 호출자 버퍼로 데이터를 복사한 후
			//       슬롯을 풀에 반납한다. send 완료 시에는 슬롯만 반납한다.
			if (op.mType == AsyncIOType::Recv)
			{
				if (res > 0 && op.mCallerBuffer)
					std::memcpy(op.mCallerBuffer, op.mPoolSlotPtr,
								static_cast<size_t>(res));
				mRecvPool.Release(op.mPoolSlotIndex);
			}
			else if (op.mType == AsyncIOType::Send)
			{
				mSendPool.Release(op.mPoolSlotIndex);
			}

			mPendingOps.erase(it);
			mStats.mPendingRequests--;
			mStats.mTotalCompletions++;
			processedCount++;
		}
	}

	if (processedCount > 0)
	{
		io_uring_cq_advance(&mRing, static_cast<unsigned>(processedCount));
	}

	return processedCount;
}

// =============================================================================
// English: Helper Methods
// 한글: 헬퍼 메서드
// =============================================================================

bool IOUringAsyncIOProvider::SubmitRing()
{
	int ret = io_uring_submit(&mRing);
	if (ret < 0)
	{
		mLastError = "io_uring_submit failed";
	}
	return ret >= 0;
}

// =============================================================================
// English: Information & Statistics
// 한글: 정보 및 통계
// =============================================================================

const ProviderInfo &IOUringAsyncIOProvider::GetInfo() const { return mInfo; }

ProviderStats IOUringAsyncIOProvider::GetStats() const { return mStats; }

const char *IOUringAsyncIOProvider::GetLastError() const
{
	return mLastError.c_str();
}

// =============================================================================
// English: Factory Function
// 한글: 팩토리 함수
// =============================================================================

std::unique_ptr<AsyncIOProvider> CreateIOUringProvider()
{
	return std::unique_ptr<AsyncIOProvider>(new IOUringAsyncIOProvider());
}

} // namespace Linux
} // namespace AsyncIO
} // namespace Network

#endif // defined(__linux__) && (defined(HAVE_IO_URING) || defined(HAVE_LIBURING))
