#pragma once

// English: io_uring-based AsyncIOProvider implementation for Linux kernel 5.1+
// 한글: Linux 커널 5.1+ 용 io_uring 기반 AsyncIOProvider 구현

#include "AsyncIOProvider.h"

#ifdef __linux__
#include <liburing.h>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace Network {
namespace AsyncIO {
namespace Linux {
    // =============================================================================
    // English: io_uring-based AsyncIOProvider Implementation (Linux kernel 5.1+)
    // 한글: io_uring 기반 AsyncIOProvider 구현 (Linux 커널 5.1+)
    // =============================================================================

    class IOUringAsyncIOProvider : public AsyncIOProvider
    {
    public:
        // English: Constructor
        // 한글: 생성자
        IOUringAsyncIOProvider();

        // English: Destructor - releases io_uring resources
        // 한글: 소멸자 - io_uring 리소스 해제
        virtual ~IOUringAsyncIOProvider();

        // English: Prevent copy (move-only semantics)
        // 한글: 복사 방지 (move-only 의미론)
        IOUringAsyncIOProvider(const IOUringAsyncIOProvider&) = delete;
        IOUringAsyncIOProvider& operator=(const IOUringAsyncIOProvider&) = delete;

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

        // English: Pending operation tracking
        // 한글: 대기 작업 추적
        struct PendingOperation
        {
            RequestContext mContext;              // English: User request context / 한글: 사용자 요청 컨텍스트
            AsyncIOType mType;                   // English: Operation type / 한글: 작업 타입
            SocketHandle mSocket;                // English: Socket handle / 한글: 소켓 핸들
            std::unique_ptr<uint8_t[]> mBuffer;  // English: Dynamically allocated buffer / 한글: 동적 할당 버퍼
            uint32_t mBufferSize;                // English: Buffer size / 한글: 버퍼 크기
        };

        // English: Registered buffer info
        // 한글: 등록된 버퍼 정보
        struct RegisteredBufferEntry
        {
            void* mAddress;           // English: Buffer address / 한글: 버퍼 주소
            uint32_t mSize;           // English: Buffer size / 한글: 버퍼 크기
            int32_t mBufferGroupId;   // English: Buffer group ID / 한글: 버퍼 그룹 ID
        };

        // =====================================================================
        // English: Member Variables
        // 한글: 멤버 변수
        // =====================================================================

        io_uring mRing;                          // English: io_uring ring / 한글: io_uring 링
        std::map<uint64_t, PendingOperation> mPendingOps;  // English: Pending ops by user_data / 한글: user_data별 대기 작업
        std::map<int64_t, RegisteredBufferEntry> mRegisteredBuffers;  // English: Registered buffers / 한글: 등록된 버퍼
        mutable std::mutex mMutex;               // English: Thread safety mutex / 한글: 스레드 안전성 뮤텍스
        ProviderInfo mInfo;                      // English: Provider info / 한글: 공급자 정보
        ProviderStats mStats;                    // English: Statistics / 한글: 통계
        std::string mLastError;                  // English: Last error message / 한글: 마지막 에러 메시지
        size_t mMaxConcurrentOps;                // English: Max concurrent ops / 한글: 최대 동시 작업
        int64_t mNextBufferId;                   // English: Next buffer ID / 한글: 다음 버퍼 ID
        uint64_t mNextOpKey;                     // English: Next operation key / 한글: 다음 작업 키
        bool mInitialized;                       // English: Initialization flag / 한글: 초기화 플래그
        bool mSupportsFixedBuffers;              // English: Fixed buffer support / 한글: 고정 버퍼 지원
        bool mSupportsDirectDescriptors;         // English: Direct descriptor support / 한글: 직접 디스크립터 지원

        // =====================================================================
        // English: Helper Methods
        // 한글: 헬퍼 메서드
        // =====================================================================

        // English: Submit pending operations to the ring
        // 한글: 대기 작업을 링에 제출
        bool SubmitRing();

        // English: Process completion queue entries
        // 한글: 완료 큐 항목 처리
        int ProcessCompletionQueue(CompletionEntry* entries, size_t maxEntries);
    };

}  // namespace Linux
}  // namespace AsyncIO
}  // namespace Network

#endif  // __linux__

