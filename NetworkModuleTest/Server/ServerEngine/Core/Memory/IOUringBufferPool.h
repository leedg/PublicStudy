#pragma once
// Core/Memory/IOUringBufferPool.h — Linux io_uring fixed buffer pool.
// Use InitializeFixed() to register buffers with a live io_uring instance for
// zero-copy fixed-buffer I/O.  Plain Initialize() skips registration (useful
// for non-fixed-buffer mode or pre-ring allocation).

#include "IBufferPool.h"

#if defined(__linux__)
#include <liburing.h>
#include <mutex>
#include <vector>
#include <sys/uio.h>

namespace Core {
namespace Memory {

class IOUringBufferPool : public IBufferPool {
public:
    IOUringBufferPool()  = default;
    ~IOUringBufferPool() override;

    IOUringBufferPool(const IOUringBufferPool&)            = delete;
    IOUringBufferPool& operator=(const IOUringBufferPool&) = delete;

    // Non-fixed mode: allocates slab without io_uring registration.
    bool Initialize(size_t poolSize, size_t slotSize) override;

    // Fixed-buffer mode: allocates slab and calls io_uring_register_buffers.
    // ring is referenced (not owned); caller must keep it alive until Shutdown().
    bool InitializeFixed(io_uring* ring, size_t poolSize, size_t slotSize);

    void Shutdown() override;

    BufferSlot Acquire() override;
    void       Release(size_t index) override;

    size_t SlotSize()  const override { return mSlotSize; }
    size_t PoolSize()  const override { return mPoolSize; }
    size_t FreeCount() const override;

    // English: io_uring concrete extensions (non-virtual — use concrete type directly).
    // 한글: io_uring 구체 확장 메서드 (비-가상 — 구체 타입으로 직접 호출).
    int  GetFixedBufferIndex(size_t index) const { return static_cast<int>(index); }
    bool IsFixedBufferMode()               const { return mIsFixed; }

private:
    void*               mStorage   = nullptr;
    size_t              mSlotSize  = 0;
    size_t              mPoolSize  = 0;
    bool                mIsFixed   = false;
    io_uring*           mRing      = nullptr; // external ownership (reference only)
    std::vector<iovec>  mIovecs;
    std::vector<size_t> mFreeIndices;
    mutable std::mutex  mMutex;
};

} // namespace Memory
} // namespace Core

#endif // defined(__linux__)
