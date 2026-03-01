#pragma once
// Core/Memory/StandardBufferPool.h — Platform-independent aligned buffer pool.
// Uses _aligned_malloc (Windows) or posix_memalign (Linux/macOS).

#include "IBufferPool.h"

#include <mutex>
#include <vector>

namespace Core {
namespace Memory {

class StandardBufferPool : public IBufferPool {
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
    void*               mStorage    = nullptr;
    size_t              mSlotSize   = 0;
    size_t              mPoolSize   = 0;
    std::vector<size_t> mFreeIndices;
    mutable std::mutex  mMutex;
};

} // namespace Memory
} // namespace Core
