#pragma once
// Core/Memory/StandardBufferPool.h — Platform-independent aligned buffer pool.
// Uses _aligned_malloc (Windows) or posix_memalign (Linux/macOS).

#include "IBufferPool.h"

#include <mutex>
#include <vector>

namespace Network
{
namespace Core
{
namespace Memory
{

class StandardBufferPool : public IBufferPool
{
public:
    StandardBufferPool()  = default;
    ~StandardBufferPool() override;

    StandardBufferPool(const StandardBufferPool&)            = delete;
    StandardBufferPool& operator=(const StandardBufferPool&) = delete;

    bool Initialize(size_t poolSize, size_t slotSize) override;
    void Shutdown() override;

    BufferSlot Acquire() override;
    void       Release(size_t index) override;

    size_t SlotSize()  const override { return mSlotSize; }
    size_t PoolSize()  const override { return mPoolSize; }
    size_t FreeCount() const override;

private:
    void*               mStorage    = nullptr; // 정렬 할당된 슬랩 메모리 시작 주소 (4096 바이트 정렬)
    size_t              mSlotSize   = 0;        // 슬롯 하나의 크기 (바이트) — Initialize 이후 불변
    size_t              mPoolSize   = 0;        // 전체 슬롯 수 — Initialize 이후 불변
    std::vector<size_t> mFreeIndices;           // 반환된 슬롯 인덱스 스택 (후입선출); mMutex로 보호
    mutable std::mutex  mMutex;                 // mStorage, mFreeIndices, mSlotSize, mPoolSize 보호용
};

} // namespace Memory
} // namespace Core
} // namespace Network
