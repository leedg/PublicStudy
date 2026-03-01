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

bool SendBufferPool::Initialize(size_t poolSize, size_t slotSize)
{
    std::lock_guard<std::mutex> lock(mMutex);

    if (poolSize == 0 || slotSize == 0)
        return false;

    mSlotSize = slotSize;
    mPoolSize = poolSize;

    // English: Single aligned contiguous allocation for all slots.
    // 한글: 모든 슬롯을 위한 단일 정렬 연속 할당.
    mStorage = _aligned_malloc(poolSize * slotSize, 64);
    if (!mStorage)
        return false;
    memset(mStorage, 0, poolSize * slotSize);

    // English: Initialize free-list stack with all indices (0 .. poolSize-1).
    // 한글: 프리리스트 스택을 모든 인덱스(0 .. poolSize-1)로 초기화.
    mFreeSlots.resize(poolSize);
    std::iota(mFreeSlots.begin(), mFreeSlots.end(), size_t(0));

    return true;
}

void SendBufferPool::Shutdown()
{
    std::lock_guard<std::mutex> lock(mMutex);
    if (mStorage)
    {
        _aligned_free(mStorage);
        mStorage = nullptr;
    }
    mFreeSlots.clear();
    mSlotSize = 0;
    mPoolSize = 0;
}

::Core::Memory::BufferSlot SendBufferPool::Acquire()
{
    std::lock_guard<std::mutex> lock(mMutex);

    if (mFreeSlots.empty() || !mStorage)
        return {};

    const size_t idx = mFreeSlots.back();
    mFreeSlots.pop_back();

    ::Core::Memory::BufferSlot slot;
    slot.ptr      = static_cast<char *>(mStorage) + idx * mSlotSize;
    slot.index    = idx;
    slot.capacity = mSlotSize;
    return slot;
}

void SendBufferPool::Release(size_t slotIdx)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mFreeSlots.push_back(slotIdx);
}

size_t SendBufferPool::SlotSize() const { return mSlotSize; }

size_t SendBufferPool::PoolSize() const { return mPoolSize; }

size_t SendBufferPool::FreeCount() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mFreeSlots.size();
}

char *SendBufferPool::SlotPtr(size_t idx) const
{
    // English: Stable after Initialize() — no lock needed.
    // 한글: Initialize() 이후 불변 — 락 불필요.
    return static_cast<char *>(mStorage) + idx * mSlotSize;
}

} // namespace Network::Core

#endif // _WIN32
