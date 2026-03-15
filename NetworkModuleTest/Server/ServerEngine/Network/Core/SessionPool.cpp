// English: SessionPool implementation
// 한글: SessionPool 구현

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
        // English: Build immutable OVERLAPPED→IOType map from fixed slot addresses.
        //          Sessions never move (stored in array), so these pointers are stable.
        // 한글: 고정 슬롯 주소로 불변 OVERLAPPED→IOType 맵 구축.
        //       세션은 배열에 저장되어 이동하지 않으므로 포인터가 안정적.
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

    // English: Custom deleter captures slotIdx — no pointer arithmetic needed.
    // 한글: Custom deleter가 slotIdx를 캡처 — 포인터 산술 불필요.
    return SessionRef(&slot.session, [this, slotIdx](Session *) { ReleaseInternal(slotIdx); });
}

void SessionPool::ReleaseInternal(size_t slotIdx)
{
    if (!mSlots || slotIdx >= mCapacity)
        return;

    PoolSlot &slot = mSlots[slotIdx];

    // English: Full teardown sequence for pool reuse:
    //   1. Close()              — closes socket, cancels AsyncScope (no blocking)
    //   2. WaitForPendingTasks()— blocks until all in-flight logic tasks complete
    //                             (pool sessions skip ~Session() so ~AsyncScope() never runs)
    //   3. Reset()              — clears state including mRecvAccumBuffer
    //                             (safe only after WaitForPendingTasks; AsyncScope::Reset()
    //                              asserts mInFlight == 0)
    // 한글: 풀 재사용을 위한 완전한 teardown 순서:
    //   1. Close()              — 소켓 닫기, AsyncScope 취소 (블로킹 없음)
    //   2. WaitForPendingTasks()— 모든 in-flight 로직 태스크 완료까지 블로킹
    //                             (풀 세션은 ~Session() 미호출로 ~AsyncScope() 미실행)
    //   3. Reset()              — mRecvAccumBuffer 포함 상태 초기화
    //                             (WaitForPendingTasks 이후에만 안전; AsyncScope::Reset()이
    //                              mInFlight == 0을 assert함)
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

    // English: mIOContextMap is immutable after Initialize() — no lock needed.
    // 한글: mIOContextMap은 Initialize() 이후 불변 — 락 불필요.
    const auto it = mIOContextMap.find(ov);
    if (it == mIOContextMap.end())
        return false;

    outType = it->second;
    return true;
}
#endif

} // namespace Network::Core
