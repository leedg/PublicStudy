#pragma once

// English: Pre-allocated Session pool + OVERLAPPED→IOType map (Windows only).
//          Eliminates per-accept heap allocation and removes the global
//          gIOTypeRegistry mutex from Session.cpp.
//
// 한글: 사전 할당 세션 풀 + OVERLAPPED→IOType 역매핑 (Windows 전용).
//       Accept마다 발생하는 힙 할당을 제거하고 Session.cpp의
//       전역 gIOTypeRegistry 뮤텍스를 삭제한다.
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

    // English: One-time initialization; call before first Acquire().
    //          Internally allocates capacity Session objects and builds
    //          the immutable IOContextMap (Windows only).
    // 한글: 최초 1회 초기화. Acquire() 전에 호출해야 한다.
    //       내부적으로 capacity개의 Session 객체를 할당하고
    //       불변 IOContextMap을 구축한다 (Windows 전용).
    bool Initialize(size_t capacity);

    void Shutdown();

    // English: Acquire a free Session from the pool.
    //          The returned shared_ptr's deleter automatically returns the
    //          session to the pool when the last reference is dropped.
    //          Returns nullptr if the pool is exhausted.
    // 한글: 풀에서 빈 세션 획득.
    //       반환된 shared_ptr의 deleter가 마지막 참조 소멸 시 자동 반납.
    //       풀 고갈 시 nullptr 반환.
    SessionRef Acquire();

    // English: Read-only OVERLAPPED→IOType lookup (lock-free after Init).
    // 한글: 읽기 전용 OVERLAPPED→IOType 조회 (Init 이후 lock-free).
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

    // English: alignas(64) keeps hot atomic fields on separate cache lines.
    // 한글: alignas(64)로 핫 atomic 필드를 별도 캐시 라인에 배치.
    struct alignas(64) PoolSlot
    {
        Session            session;
        std::atomic<bool>  inUse{false};
        size_t             slotIdx{0};
    };

    // English: Return slot by index (captured in shared_ptr deleter — no pointer arithmetic).
    // 한글: 인덱스로 슬롯 반납 (shared_ptr deleter에서 캡처 — 포인터 산술 불필요).
    void ReleaseInternal(size_t slotIdx);

    std::unique_ptr<PoolSlot[]>  mSlots;
    size_t                       mCapacity{0};

    // English: O(1) free-list stack protected by mFreeListMutex.
    // 한글: O(1) 프리리스트 스택 (mFreeListMutex 보호).
    std::vector<size_t> mFreeList;
    std::mutex          mFreeListMutex;

#ifdef _WIN32
    // English: Immutable after Initialize(); multi-thread reads need no lock.
    // 한글: Initialize() 이후 불변 — 다중 스레드 읽기에 락 불필요.
    std::unordered_map<const OVERLAPPED *, IOType> mIOContextMap;
#endif

    std::atomic<size_t> mActiveCount{0};
    bool                mInitialized{false};
};

} // namespace Network::Core
