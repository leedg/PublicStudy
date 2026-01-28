#pragma once

// English: epoll-based AsyncIOProvider implementation for Linux
// 한글: Linux용 epoll 기반 AsyncIOProvider 구현

#include "AsyncIOProvider.h"

#ifdef __linux__
#include <sys/epoll.h>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

namespace Network::AsyncIO::Linux
{
    // =============================================================================
    // English: epoll-based AsyncIOProvider Implementation
    // 한글: epoll 기반 AsyncIOProvider 구현
    // =============================================================================

    class EpollAsyncIOProvider : public AsyncIOProvider
    {
    public:
        // English: Constructor
        // 한글: 생성자
        EpollAsyncIOProvider();

        // English: Destructor - releases epoll resources
        // 한글: 소멸자 - epoll 리소스 해제
        virtual ~EpollAsyncIOProvider();

        // English: Prevent copy (move-only semantics)
        // 한글: 복사 방지 (move-only 의미론)
        EpollAsyncIOProvider(const EpollAsyncIOProvider&) = delete;
        EpollAsyncIOProvider& operator=(const EpollAsyncIOProvider&) = delete;

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

        int64_t RegisterBuffer(const void* ptr, size_t size) override;
        AsyncIOError UnregisterBuffer(int64_t bufferId) override;

        // =====================================================================
        // English: Async I/O Requests
        // 한글: 비동기 I/O 요청
        // =====================================================================

        AsyncIOError SendAsync(
            SocketHandle socket,
            const void* buffer,
            size_t size,
            RequestContext context,
            uint32_t flags = 0
        ) override;

        AsyncIOError RecvAsync(
            SocketHandle socket,
            void* buffer,
            size_t size,
            RequestContext context,
            uint32_t flags = 0
        ) override;

        AsyncIOError FlushRequests() override;

        // =====================================================================
        // English: Completion Processing
        // 한글: 완료 처리
        // =====================================================================

        int ProcessCompletions(
            CompletionEntry* entries,
            size_t maxEntries,
            int timeoutMs = 0
        ) override;

        // =====================================================================
        // English: Information & Statistics
        // 한글: 정보 및 통계
        // =====================================================================

        const ProviderInfo& GetInfo() const override;
        ProviderStats GetStats() const override;
        const char* GetLastError() const override;

    private:
        // =====================================================================
        // English: Internal Data Structures
        // 한글: 내부 데이터 구조
        // =====================================================================

        // English: Pending operation tracking structure
        // 한글: 대기 중인 작업 추적 구조체
        struct PendingOperation
        {
            RequestContext mContext;              // English: User request context / 한글: 사용자 요청 컨텍스트
            AsyncIOType mType;                   // English: Operation type / 한글: 작업 타입
            std::unique_ptr<uint8_t[]> mBuffer;  // English: Dynamically allocated buffer / 한글: 동적 할당 버퍼
            uint32_t mBufferSize;                // English: Buffer size / 한글: 버퍼 크기
        };

        // =====================================================================
        // English: Member Variables
        // 한글: 멤버 변수
        // =====================================================================

        int mEpollFd;                            // English: epoll file descriptor / 한글: epoll 파일 디스크립터
        std::map<SocketHandle, PendingOperation> mPendingOps;  // English: Pending operations / 한글: 대기 작업
        mutable std::mutex mMutex;               // English: Thread safety mutex / 한글: 스레드 안전성 뮤텍스
        ProviderInfo mInfo;                      // English: Provider info / 한글: 공급자 정보
        ProviderStats mStats;                    // English: Statistics / 한글: 통계
        std::string mLastError;                  // English: Last error message / 한글: 마지막 에러 메시지
        size_t mMaxConcurrentOps;                // English: Max concurrent ops / 한글: 최대 동시 작업
        bool mInitialized;                       // English: Initialization flag / 한글: 초기화 플래그
    };

}  // namespace Network::AsyncIO::Linux

#endif  // __linux__
