#pragma once

// English: kqueue-based AsyncIOProvider implementation for macOS/BSD
// 한글: macOS/BSD용 kqueue 기반 AsyncIOProvider 구현

#include "AsyncIOProvider.h"

#ifdef __APPLE__
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <sys/event.h>

namespace Network
{
namespace AsyncIO
{
namespace BSD
{
// =============================================================================
// English: kqueue-based AsyncIOProvider Implementation (macOS/BSD)
// 한글: kqueue 기반 AsyncIOProvider 구현 (macOS/BSD)
// =============================================================================

class KqueueAsyncIOProvider : public AsyncIOProvider
{
  public:
	// English: Constructor
	// 한글: 생성자
	KqueueAsyncIOProvider();

	// English: Destructor - releases kqueue resources
	// 한글: 소멸자 - kqueue 리소스 해제
	virtual ~KqueueAsyncIOProvider();

	// English: Prevent copy (move-only semantics)
	// 한글: 복사 방지 (move-only 의미론)
	KqueueAsyncIOProvider(const KqueueAsyncIOProvider &) = delete;
	KqueueAsyncIOProvider &operator=(const KqueueAsyncIOProvider &) = delete;

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
	// English: Internal Data Structures
	// 한글: 내부 데이터 구조
	// =====================================================================

	// English: Pending operation tracking
	// 한글: 대기 작업 추적
	struct PendingOperation
	{
		RequestContext mContext; // English: User request context / 한글: 사용자
								 // 요청 컨텍스트
		AsyncIOType mType;    // English: Operation type / 한글: 작업 타입
		SocketHandle mSocket; // English: Socket handle / 한글: 소켓 핸들
		std::unique_ptr<uint8_t[]> mBuffer; // English: Buffer / 한글: 버퍼
		uint32_t mBufferSize; // English: Buffer size / 한글: 버퍼 크기
	};

	// =====================================================================
	// English: Member Variables
	// 한글: 멤버 변수
	// =====================================================================

	int mKqueueFd; // English: kqueue file descriptor / 한글: kqueue 파일
					   // 디스크립터
	std::map<SocketHandle, PendingOperation>
		mPendingOps; // English: Pending ops / 한글: 대기 작업
	std::map<SocketHandle, bool>
		mRegisteredSockets; // English: Registered sockets / 한글: 등록된 소켓
	mutable std::mutex
		mMutex; // English: Thread safety mutex / 한글: 스레드 안전성 뮤텍스
	ProviderInfo mInfo;   // English: Provider info / 한글: 공급자 정보
	ProviderStats mStats; // English: Statistics / 한글: 통계
	std::string
		mLastError; // English: Last error message / 한글: 마지막 에러 메시지
	size_t
		mMaxConcurrentOps; // English: Max concurrent ops / 한글: 최대 동시 작업
	bool mInitialized;     // English: Initialization flag / 한글: 초기화 플래그

	// =====================================================================
	// English: Helper Methods
	// 한글: 헬퍼 메서드
	// =====================================================================

	// English: Register socket with kqueue for read and write events
	// 한글: kqueue에 소켓을 읽기/쓰기 이벤트로 등록
	bool RegisterSocketEvents(SocketHandle socket);

	// English: Unregister socket events from kqueue
	// 한글: kqueue에서 소켓 이벤트 등록 해제
	bool UnregisterSocketEvents(SocketHandle socket);
};

} // namespace BSD
} // namespace AsyncIO
} // namespace Network

#endif // __APPLE__
