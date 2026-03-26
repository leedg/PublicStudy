// SendBufferPool 구현

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
    mPoolSize.store(poolSize, std::memory_order_relaxed);

    // 모든 슬롯을 위한 단일 64바이트 정렬 연속 할당.
    // 64바이트 정렬: 각 슬롯이 캐시 라인 경계에서 시작하도록 하여
    // false sharing과 비정렬 SIMD 접근을 방지한다.
    mStorage = _aligned_malloc(poolSize * slotSize, 64);
    if (!mStorage)
        return false;
    memset(mStorage, 0, poolSize * slotSize);

    // 프리리스트 스택을 모든 인덱스(0 .. poolSize-1)로 초기화.
    // std::iota로 0부터 연속 채움 — Acquire()에서 back(), pop_back()으로 O(1) 대여.
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
    mPoolSize.store(0, std::memory_order_relaxed);
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
    // English: Bounds check before the lock — mPoolSize is std::atomic so no data race.
    //          mPoolSize is immutable after Initialize(); only Shutdown() resets it to 0.
    // 한글: 락 밖에서 bounds check — mPoolSize는 atomic이므로 data race 없음.
    //       Initialize() 이후 불변; Shutdown()만 0으로 리셋.
    if (slotIdx >= mPoolSize.load(std::memory_order_acquire))
    {
        assert(false && "SendBufferPool::Release: slotIdx out of range");
        return;
    }

    std::lock_guard<std::mutex> lock(mMutex);
    // English: Guard against Shutdown() racing between the bounds check and here.
    //          If mStorage is null, the pool was destroyed — skip the push_back.
    // 한글: bounds check 이후 Shutdown()이 끼어드는 레이스 방어.
    //       mStorage가 null이면 풀이 해제됨 — push_back 생략.
    if (!mStorage)
        return;
    mFreeSlots.push_back(slotIdx);
}

size_t SendBufferPool::SlotSize() const { return mSlotSize; }

size_t SendBufferPool::PoolSize() const { return mPoolSize.load(std::memory_order_acquire); }

size_t SendBufferPool::FreeCount() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mFreeSlots.size();
}

char *SendBufferPool::SlotPtr(size_t idx) const
{
    // mStorage와 mSlotSize는 Initialize() 이후 불변이므로 락 없이 읽어도 안전.
    return static_cast<char *>(mStorage) + idx * mSlotSize;
}

} // namespace Network::Core

#endif // _WIN32
