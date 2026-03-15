// SendBufferPool implementation

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

    // Single aligned contiguous allocation for all slots.
    mStorage = _aligned_malloc(poolSize * slotSize, 64);
    if (!mStorage)
        return false;
    memset(mStorage, 0, poolSize * slotSize);

    // Initialize free-list stack with all indices (0 .. poolSize-1).
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

::Network::Core::Memory::BufferSlot SendBufferPool::Acquire()
{
    std::lock_guard<std::mutex> lock(mMutex);

    if (mFreeSlots.empty() || !mStorage)
        return {};

    const size_t idx = mFreeSlots.back();
    mFreeSlots.pop_back();

    ::Network::Core::Memory::BufferSlot slot;
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
    // Stable after Initialize() — no lock needed.
    return static_cast<char *>(mStorage) + idx * mSlotSize;
}

} // namespace Network::Core

#endif // _WIN32
