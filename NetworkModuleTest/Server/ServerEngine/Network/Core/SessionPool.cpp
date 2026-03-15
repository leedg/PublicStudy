// SessionPool implementation

#include "SessionPool.h"
#include "../../Utils/Logger.h"

namespace Network::Core
{

SessionPool &SessionPool::Instance()
{
    static SessionPool instance;
    return instance;
}

bool SessionPool::Initialize(size_t capacity)
{
    if (mInitialized)
    {
        Utils::Logger::Warn("SessionPool already initialized");
        return true;
    }

    if (capacity == 0)
        return false;

    mCapacity = capacity;
    mSlots    = std::make_unique<PoolSlot[]>(capacity);

    mFreeList.reserve(capacity);
    for (size_t i = 0; i < capacity; ++i)
    {
        mSlots[i].slotIdx = i;
        mFreeList.push_back(i);

#ifdef _WIN32
        // Build immutable OVERLAPPED→IOType map from fixed slot addresses.
        //          Sessions never move (stored in array), so these pointers are stable.
        mIOContextMap[static_cast<const OVERLAPPED *>(
            &mSlots[i].session.GetRecvContext())] = IOType::Recv;
        mIOContextMap[static_cast<const OVERLAPPED *>(
            &mSlots[i].session.GetSendContext())] = IOType::Send;
#endif
    }

    mInitialized = true;
    Utils::Logger::Info("SessionPool initialized: capacity=" + std::to_string(capacity));
    return true;
}

void SessionPool::Shutdown()
{
    if (!mInitialized)
        return;

    mInitialized = false;

    std::lock_guard<std::mutex> lock(mFreeListMutex);
    mFreeList.clear();
    mSlots.reset();
    mCapacity = 0;
    mActiveCount.store(0, std::memory_order_relaxed);

#ifdef _WIN32
    mIOContextMap.clear();
#endif

    Utils::Logger::Info("SessionPool shut down");
}

SessionRef SessionPool::Acquire()
{
    size_t slotIdx = ~size_t(0);
    {
        std::lock_guard<std::mutex> lock(mFreeListMutex);
        if (mFreeList.empty())
            return nullptr;

        slotIdx = mFreeList.back();
        mFreeList.pop_back();
    }

    PoolSlot &slot = mSlots[slotIdx];
    slot.inUse.store(true, std::memory_order_release);
    mActiveCount.fetch_add(1, std::memory_order_relaxed);

    // Custom deleter captures slotIdx — no pointer arithmetic needed.
    return SessionRef(&slot.session, [this, slotIdx](Session *) { ReleaseInternal(slotIdx); });
}

void SessionPool::ReleaseInternal(size_t slotIdx)
{
    if (!mSlots || slotIdx >= mCapacity)
        return;

    PoolSlot &slot = mSlots[slotIdx];

    // Full teardown sequence for pool reuse:
    //   1. Close()              — closes socket, cancels AsyncScope (no blocking)
    //   2. WaitForPendingTasks()— blocks until all in-flight logic tasks complete
    //                             (pool sessions skip ~Session() so ~AsyncScope() never runs)
    //   3. Reset()              — clears state including mRecvAccumBuffer
    //                             (safe only after WaitForPendingTasks; AsyncScope::Reset()
    //                              asserts mInFlight == 0)
    slot.session.Close();
    slot.session.WaitForPendingTasks();
    slot.session.Reset();

    slot.inUse.store(false, std::memory_order_release);
    mActiveCount.fetch_sub(1, std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(mFreeListMutex);
        mFreeList.push_back(slotIdx);
    }
}

#ifdef _WIN32
bool SessionPool::ResolveIOType(const OVERLAPPED *ov, IOType &outType) const
{
    if (!ov)
        return false;

    // mIOContextMap is immutable after Initialize() — no lock needed.
    const auto it = mIOContextMap.find(ov);
    if (it == mIOContextMap.end())
        return false;

    outType = it->second;
    return true;
}
#endif

} // namespace Network::Core
