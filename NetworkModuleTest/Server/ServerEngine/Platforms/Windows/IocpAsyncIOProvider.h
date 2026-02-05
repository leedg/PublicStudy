#pragma once

// English: IOCP-based AsyncIOProvider implementation for Windows
// 한글: Windows용 IOCP 기반 AsyncIOProvider 구현
//
// =============================================================================
// Relationship with IOCPNetworkEngine / IOCPNetworkEngine과의 관계
// =============================================================================
//
// English:
// IocpAsyncIOProvider and IOCPNetworkEngine serve DIFFERENT purposes:
//
// IocpAsyncIOProvider:
//   - Low-level IOCP abstraction for AsyncIOProvider interface
//   - Platform-independent design (can swap with RIO/epoll/io_uring)
//   - Session-independent I/O operations
//   - Used for multi-platform libraries or advanced scenarios
//
// IOCPNetworkEngine:
//   - High-level server engine with Session management
//   - Optimized for Windows server applications
//   - Session lifecycle, event callbacks, thread pools
//   - Direct IOCP usage with Session::IOContext
//
// 한글:
// IocpAsyncIOProvider와 IOCPNetworkEngine은 다른 목적을 가집니다:
//
// IocpAsyncIOProvider:
//   - AsyncIOProvider 인터페이스를 위한 저수준 IOCP 추상화
//   - 플랫폼 독립적 설계 (RIO/epoll/io_uring와 교체 가능)
//   - Session과 독립적인 I/O 작업
//   - 멀티플랫폼 라이브러리 또는 고급 시나리오에 사용
//
// IOCPNetworkEngine:
//   - Session 관리를 포함한 고수준 서버 엔진
//   - Windows 서버 애플리케이션에 최적화
//   - Session 생명주기, 이벤트 콜백, 스레드 풀
//   - Session::IOContext로 직접 IOCP 사용
// =============================================================================

#include "Network/Core/AsyncIOProvider.h"

#ifdef _WIN32
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace Network
{
namespace AsyncIO
{
namespace Windows
{
// =============================================================================
// English: IOCP-based AsyncIOProvider Implementation
// 한글: IOCP 기반 AsyncIOProvider 구현
// =============================================================================

class IocpAsyncIOProvider : public AsyncIOProvider
{
  public:
	// English: Constructor
	// 한글: 생성자
	IocpAsyncIOProvider();

	// English: Destructor - releases IOCP resources
	// 한글: 소멸자 - IOCP 리소스 해제
	virtual ~IocpAsyncIOProvider();

	// English: Prevent copy (move-only semantics)
	// 한글: 복사 방지 (move-only 의미론)
	IocpAsyncIOProvider(const IocpAsyncIOProvider &) = delete;
	IocpAsyncIOProvider &operator=(const IocpAsyncIOProvider &) = delete;

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

	// English: Pending operation tracking structure
	// 한글: 대기 중인 작업 추적 구조체
	struct PendingOperation
	{
		OVERLAPPED mOverlapped; // English: IOCP overlapped structure / 한글:
								// IOCP 오버랩 구조체
		WSABUF mWsaBuffer;      // English: WSA buffer / 한글: WSA 버퍼
		std::unique_ptr<uint8_t[]> mBuffer; // English: Dynamically allocated
											// buffer / 한글: 동적 할당 버퍼
		RequestContext mContext; // English: User request context / 한글: 사용자
								 // 요청 컨텍스트
		AsyncIOType mType; // English: Operation type / 한글: 작업 타입
	};

	// =====================================================================
	// English: Member Variables
	// 한글: 멤버 변수
	// =====================================================================

	HANDLE mCompletionPort; // English: IOCP completion port handle / 한글: IOCP
							// 완료 포트 핸들
	std::map<SocketHandle, std::unique_ptr<PendingOperation>>
		mPendingOps; // English: Pending ops / 한글: 대기 작업
	mutable std::mutex
		mMutex; // English: Thread safety mutex / 한글: 스레드 안전성 뮤텍스
	ProviderInfo mInfo; // English: Provider info cache / 한글: 공급자 정보 캐시
	ProviderStats mStats; // English: Statistics / 한글: 통계
	std::string
		mLastError; // English: Last error message / 한글: 마지막 에러 메시지
	size_t mMaxConcurrentOps; // English: Max concurrent ops / 한글: 최대 동시
								  // 작업 수
	bool mInitialized; // English: Initialization flag / 한글: 초기화 플래그
};

} // namespace Windows
} // namespace AsyncIO
} // namespace Network

#endif // _WIN32
