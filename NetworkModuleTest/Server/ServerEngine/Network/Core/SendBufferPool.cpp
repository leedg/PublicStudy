// English: SendBufferPool implementation
// 한글: SendBufferPool 구현

#ifdef _WIN32

#include "SendBufferPool.h"
#include <cassert>
#include <stdexcept>

namespace Network::Core
{

SendBufferPool &SendBufferPool::Instance()
{
    static SendBufferPool instance;
    return instance;
}

void SendBufferPool::Initialize(size_t poolSize, size_t slotSize)
{
    std::lock_guard<std::mutex> lock(mMutex);

    if (poolSize == 0 || slotSize == 0)
        return;

    mSlotSize = slotSize;
    mPoolSize = poolSize;

    // English: Single contiguous allocation for all slots.
    // 한글: 모든 슬롯을 위한 단일 연속 할당.
    mStorage.resize(poolSize * slotSize, '\0');

    // English: Initialize free-list stack with all indices (0 .. poolSize-1).
    // 한글: 프리리스트 스택을 모든 인덱스(0 .. poolSize-1)로 초기화.
    mFreeSlots.resize(poolSize);
    std::iota(mFreeSlots.begin(), mFreeSlots.end(), size_t(0));
}

SendBufferPool::Slot SendBufferPool::Acquire()
{
    std::lock_guard<std::mutex> lock(mMutex);

    if (mFreeSlots.empty())
        return {nullptr, ~size_t(0)};

    const size_t idx = mFreeSlots.back();
    mFreeSlots.pop_back();
    return {mStorage.data() + idx * mSlotSize, idx};
}

void SendBufferPool::Release(size_t slotIdx)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mFreeSlots.push_back(slotIdx);
}

char *SendBufferPool::SlotPtr(size_t idx)
{
    // English: Stable after Initialize() — no lock needed.
    // 한글: Initialize() 이후 불변 — 락 불필요.
    return mStorage.data() + idx * mSlotSize;
}

} // namespace Network::Core

#endif // _WIN32
