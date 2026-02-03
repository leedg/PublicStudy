#pragma once

// English: RIO (Registered I/O) based AsyncIOProvider implementation for
// Windows 8+ 한글: Windows 8+ 용 RIO (등록 I/O) 기반 AsyncIOProvider 구현

#include "Network/Core/AsyncIOProvider.h"

#ifdef _WIN32
#include <map>
#include <memory>
#include <mswsock.h>
#include <mutex>
#include <string>

namespace Network
{
namespace AsyncIO
{
namespace Windows
{
// =============================================================================
// English: RIO (Registered I/O) based AsyncIOProvider Implementation
// 한글: RIO (등록 I/O) 기반 AsyncIOProvider 구현
// =============================================================================

class RIOAsyncIOProvider : public AsyncIOProvider
{
  public:
	// English: Constructor
	// 한글: 생성자
	RIOAsyncIOProvider();

	// English: Destructor - releases RIO resources
	// 한글: 소멸자 - RIO 리소스 해제
	virtual ~RIOAsyncIOProvider();

	// English: Prevent copy (move-only semantics)
	// 한글: 복사 방지 (move-only 의미론)
	RIOAsyncIOProvider(const RIOAsyncIOProvider &) = delete;
	RIOAsyncIOProvider &operator=(const RIOAsyncIOProvider &) = delete;

	// =====================================================================
	// English: Lifecycle Management
	// 한글: 생명주기 관리
	// =====================================================================

	AsyncIOError Initialize(size_t queueDepth, size_t maxConcurrent) override;
	void Shutdown() override;
	bool IsInitialized() const override;

	// =====================================================================
	// English: Buffer Management
	// 한글: 버퍼 관리
	// =====================================================================

	int64_t RegisterBuffer(const void *ptr, size_t size) override;
	AsyncIOError UnregisterBuffer(int64_t bufferId) override;

	// =====================================================================
	// English: Async I/O Requests
	// 한글: 비동기 I/O 요청
	// =====================================================================

	AsyncIOError SendAsync(SocketHandle socket, const void *buffer, size_t size,
							   RequestContext context, uint32_t flags = 0) override;

	AsyncIOError RecvAsync(SocketHandle socket, void *buffer, size_t size,
							   RequestContext context, uint32_t flags = 0) override;

	AsyncIOError FlushRequests() override;

	// =====================================================================
	// English: Completion Processing
	// 한글: 완료 처리
	// =====================================================================

	int ProcessCompletions(CompletionEntry *entries, size_t maxEntries,
							   int timeoutMs = 0) override;

	// =====================================================================
	// English: Information & Statistics
	// 한글: 정보 및 통계
	// =====================================================================

	const ProviderInfo &GetInfo() const override;
	ProviderStats GetStats() const override;
	const char *GetLastError() const override;

  private:
	// =====================================================================
	// English: RIO Function Pointer Types
	// 한글: RIO 함수 포인터 타입
	// =====================================================================

	typedef int(WSAAPI *PfnRIOCloseCompletionQueue)(_In_ RIO_CQ cq);
	typedef RIO_CQ(WSAAPI *PfnRIOCreateCompletionQueue)(
		_In_ DWORD cqSize,
		_In_opt_ PRIO_NOTIFICATION_COMPLETION notificationCompletion);
	typedef RIO_RQ(WSAAPI *PfnRIOCreateRequestQueue)(
		_In_ SOCKET socket, _In_ DWORD maxOutstandingSend,
		_In_ DWORD maxOutstandingRecv, _In_ RIO_CQ cq);
	typedef ULONG(WSAAPI *PfnRIODequeueCompletion)(
		_In_ RIO_CQ cq, _Out_writes_to_(arraySize, return) PRIORESULT array,
		_In_ ULONG arraySize);
	typedef void(WSAAPI *PfnRIONotify)(_In_ RIO_CQ cq);
	typedef RIO_BUFFERID(WSAAPI *PfnRIORegisterBuffer)(_In_ PCHAR dataBuffer,
														   _In_ DWORD dataLength);
	typedef int(WSAAPI *PfnRIODeregisterBuffer)(_In_ RIO_BUFFERID bufferId);
	typedef int(WSAAPI *PfnRIOSend)(_In_ RIO_RQ requestQueue,
									_In_reads_(dataBufferCount)
										PRIO_BUF dataBuffers,
									_In_ DWORD dataBufferCount,
									_In_ DWORD flags,
									_In_ void *requestContext);
	typedef int(WSAAPI *PfnRIORecv)(_In_ RIO_RQ requestQueue,
									_In_reads_(dataBufferCount)
										PRIO_BUF dataBuffers,
									_In_ DWORD dataBufferCount,
									_In_ DWORD flags,
									_In_ void *requestContext);

	// =====================================================================
	// English: Internal Data Structures
	// 한글: 내부 데이터 구조
	// =====================================================================

	// English: Registered buffer info
	// 한글: 등록된 버퍼 정보
	struct RegisteredBufferEntry
	{
		RIO_BUFFERID mRioBufferId; // English: RIO buffer ID / 한글: RIO 버퍼 ID
		void *mBufferPtr;     // English: Buffer pointer / 한글: 버퍼 포인터
		uint32_t mBufferSize; // English: Buffer size / 한글: 버퍼 크기
	};

	// English: Pending operation tracking
	// 한글: 대기 작업 추적
	struct PendingOperation
	{
		RequestContext mContext; // English: User request context / 한글: 사용자
								 // 요청 컨텍스트
		SocketHandle mSocket; // English: Socket handle / 한글: 소켓 핸들
		AsyncIOType mType;    // English: Operation type / 한글: 작업 타입
	};

	// =====================================================================
	// English: Member Variables
	// 한글: 멤버 변수
	// =====================================================================

	RIO_CQ
		mCompletionQueue; // English: RIO completion queue / 한글: RIO 완료 큐
	std::map<SocketHandle, RIO_RQ>
		mRequestQueues; // English: Request queues per socket / 한글: 소켓별
						// 요청 큐
	std::map<int64_t, RegisteredBufferEntry>
		mRegisteredBuffers; // English: Registered buffers / 한글: 등록된 버퍼
	std::map<void *, PendingOperation>
		mPendingOps; // English: Pending operations / 한글: 대기 작업
	mutable std::mutex
		mMutex; // English: Thread safety mutex / 한글: 스레드 안전성 뮤텍스

	// English: RIO function pointers (mVariableName convention)
	// 한글: RIO 함수 포인터 (mVariableName 규칙)
	PfnRIOCloseCompletionQueue mPfnRIOCloseCompletionQueue;
	PfnRIOCreateCompletionQueue mPfnRIOCreateCompletionQueue;
	PfnRIOCreateRequestQueue mPfnRIOCreateRequestQueue;
	PfnRIODequeueCompletion mPfnRIODequeueCompletion;
	PfnRIONotify mPfnRIONotify;
	PfnRIORegisterBuffer mPfnRIORegisterBuffer;
	PfnRIODeregisterBuffer mPfnRIODeregisterBuffer;
	PfnRIOSend mPfnRIOSend;
	PfnRIORecv mPfnRIORecv;

	ProviderInfo mInfo;   // English: Provider info / 한글: 공급자 정보
	ProviderStats mStats; // English: Statistics / 한글: 통계
	std::string
		mLastError; // English: Last error message / 한글: 마지막 에러 메시지
	size_t
		mMaxConcurrentOps; // English: Max concurrent ops / 한글: 최대 동시 작업
	int64_t mNextBufferId; // English: Next buffer ID / 한글: 다음 버퍼 ID
	bool mInitialized;     // English: Initialization flag / 한글: 초기화 플래그

	// =====================================================================
	// English: Helper Methods
	// 한글: 헬퍼 메서드
	// =====================================================================

	// English: Load RIO function pointers from mswsock.dll
	// 한글: mswsock.dll에서 RIO 함수 포인터 로드
	bool LoadRIOFunctions();
};

} // namespace Windows
} // namespace AsyncIO
} // namespace Network

#endif // _WIN32
