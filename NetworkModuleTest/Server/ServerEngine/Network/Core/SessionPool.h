#pragma once

// 사전 할당 세션 풀 + OVERLAPPED→IOType 역매핑 (Windows 전용).
// Accept마다 발생하는 힙 할당을 제거하고 Session.cpp의
// 전역 gIOTypeRegistry 뮤텍스를 삭제한다.
//
// Design:
//   - Sessions are stored as PoolSlot[] (fixed addresses, no movement).
//   - Acquire() returns a shared_ptr<Session> with a custom deleter
//     that calls Reset()+Close() and marks the slot free.
//   - IOContextMap is built once in Initialize() and is read-only afterwards
//     (write: Initialize 1회, read: 다중 스레드 → mutex 불필요).

#include "Session.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#ifdef _WIN32
#include <unordered_map>
#endif

namespace Network::Core
{

class SessionPool
{
  public:
    static SessionPool &Instance();

    // 최초 1회 초기화. Acquire() 전에 호출해야 한다.
    // 내부적으로 capacity개의 Session 객체를 할당하고
    // 불변 IOContextMap을 구축한다 (Windows 전용).
    bool Initialize(size_t capacity);

    void Shutdown();

    // 풀에서 빈 세션 획득.
    // 반환된 shared_ptr의 deleter가 마지막 참조 소멸 시 자동 반납.
    // 풀 고갈 시 nullptr 반환.
    SessionRef Acquire();

    // 읽기 전용 OVERLAPPED→IOType 조회. Initialize() 이후 불변 — lock-free.
#ifdef _WIN32
    bool ResolveIOType(const OVERLAPPED *ov, IOType &outType) const;
#endif

    size_t Capacity()    const { return mCapacity; }
    size_t ActiveCount() const { return mActiveCount.load(std::memory_order_relaxed); }

  private:
    SessionPool() = default;
    ~SessionPool() { Shutdown(); }

    SessionPool(const SessionPool &) = delete;
    SessionPool &operator=(const SessionPool &) = delete;

    // alignas(64)로 핫 atomic 필드를 별도 캐시 라인에 배치 — false sharing 방지.
    struct alignas(64) PoolSlot
    {
        Session           session;          // 세션 객체 본체 — 이동하지 않아 OVERLAPPED 포인터가 안정적
        std::atomic<bool> inUse{false};     // 슬롯 사용 중 여부 — acquire/release로 가시성 보장
        size_t            slotIdx{0};       // 자기 자신의 인덱스 — deleter 캡처용 (포인터 산술 불필요)
    };

    // 인덱스로 슬롯 반납. shared_ptr deleter에서 slotIdx를 캡처하므로 포인터 산술 불필요.
    void ReleaseInternal(size_t slotIdx);

    std::unique_ptr<PoolSlot[]> mSlots;      // 사전 할당된 세션 슬롯 배열 — 고정 주소로 OVERLAPPED 포인터 안정
    size_t                      mCapacity{0}; // 슬롯 총 수 — Initialize() 이후 불변

    // O(1) 프리리스트 스택 (mFreeListMutex 보호).
    std::vector<size_t> mFreeList;            // 빈 슬롯 인덱스 스택 — back()/pop_back() 대여, push_back() 반납
    std::mutex          mFreeListMutex;       // mFreeList Acquire/Release 동시 접근 보호

#ifdef _WIN32
    // Initialize() 이후 불변 — 다중 스레드 읽기에 락 불필요.
    std::unordered_map<const OVERLAPPED *, IOType> mIOContextMap;  // OVERLAPPED* → Recv/Send 역매핑
#endif

    std::atomic<size_t> mActiveCount{0};  // 현재 대여 중인 슬롯 수 — Shutdown 시 잔여 감지용
    bool                mInitialized{false};  // 중복 Initialize() 방지 플래그
};

} // namespace Network::Core
