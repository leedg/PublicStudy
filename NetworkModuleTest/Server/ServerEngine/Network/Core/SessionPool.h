#pragma once

// Pre-allocated Session pool + OVERLAPPED→IOType map (Windows only).
//          Eliminates per-accept heap allocation and removes the global
//          gIOTypeRegistry mutex from Session.cpp.
//
//
// Design:
//   - Sessions are stored as PoolSlot[] (fixed addresses, no movement).
//   - Acquire() returns a shared_ptr<Session> with a custom deleter
//     that calls Reset()+Close() and marks the slot free.
//   - IOContextMap is built once in Initialize() and is read-only afterwards

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

    // One-time initialization; call before first Acquire().
    //          Internally allocates capacity Session objects and builds
    //          the immutable IOContextMap (Windows only).
    bool Initialize(size_t capacity);

    void Shutdown();

    // Acquire a free Session from the pool.
    //          The returned shared_ptr's deleter automatically returns the
    //          session to the pool when the last reference is dropped.
    //          Returns nullptr if the pool is exhausted.
    SessionRef Acquire();

    // Read-only OVERLAPPED→IOType lookup (lock-free after Init).
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

    // alignas(64) keeps hot atomic fields on separate cache lines.
    struct alignas(64) PoolSlot
    {
        Session            session;
        std::atomic<bool>  inUse{false};
        size_t             slotIdx{0};
    };

    // Return slot by index (captured in shared_ptr deleter — no pointer arithmetic).
    void ReleaseInternal(size_t slotIdx);

    std::unique_ptr<PoolSlot[]>  mSlots;
    size_t                       mCapacity{0};

    // O(1) free-list stack protected by mFreeListMutex.
    std::vector<size_t> mFreeList;
    std::mutex          mFreeListMutex;

#ifdef _WIN32
    // Immutable after Initialize(); multi-thread reads need no lock.
    std::unordered_map<const OVERLAPPED *, IOType> mIOContextMap;
#endif

    std::atomic<size_t> mActiveCount{0};
    bool                mInitialized{false};
};

} // namespace Network::Core
