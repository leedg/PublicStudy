#pragma once
// Core/Memory/RIOBufferPool.h — Windows RIO pre-registered slab buffer pool.
// Allocates a contiguous slab with VirtualAlloc and registers it once via
// RIORegisterBuffer.  All slots share a single RIO_BUFFERID; per-slot offset
// is index * slotSize.

#include "IBufferPool.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <mutex>
#include <vector>

namespace Network
{
namespace Core
{
namespace Memory
{

class RIOBufferPool : public IBufferPool
{
public:
    RIOBufferPool()  = default;
    ~RIOBufferPool() override;

    RIOBufferPool(const RIOBufferPool&)            = delete;
    RIOBufferPool& operator=(const RIOBufferPool&) = delete;

    // Loads RIO function pointers via a temporary socket, then VirtualAlloc +
    // 1x RIORegisterBuffer for the entire slab.
    bool Initialize(size_t poolSize, size_t slotSize) override;
    void Shutdown() override;

    BufferSlot Acquire() override; // returns {slotPtr, index, slotSize}
    void       Release(size_t index) override;

    // SlotSize/PoolSize are set once in Initialize — safe to read without lock.
    size_t SlotSize()  const override { return mSlotSize; }
    size_t PoolSize()  const override { return mPoolSize; }
    size_t FreeCount() const override;

    // RIO 구체 확장 메서드 — 비가상이므로 구체 타입(RIOBufferPool)으로 직접 호출.
    // IBufferPool* 경유 시 사용 불가 (의도적 설계: 플랫폼 ABI를 인터페이스에 노출하지 않음).
    uint64_t GetRIOBufferId(size_t index) const;
    size_t   GetRIOOffset  (size_t index) const { return index * mSlotSize; }

    // Concrete helpers — used by RIOAsyncIOProvider directly (no virtual dispatch overhead)
    RIO_BUFFERID GetSlabId()           const { return mSlabId; }
    char*        SlotPtr(size_t index) const;

private:
    void*        mSlab    = nullptr;
    RIO_BUFFERID mSlabId  = RIO_INVALID_BUFFERID;
    size_t       mSlotSize = 0;
    size_t       mPoolSize = 0;

    std::vector<size_t> mFreeIndices;
    mutable std::mutex  mMutex;

    LPFN_RIOREGISTERBUFFER   mPfnRegisterBuffer   = nullptr;
    LPFN_RIODEREGISTERBUFFER mPfnDeregisterBuffer  = nullptr;
};

} // namespace Memory
} // namespace Core
} // namespace Network

#endif // _WIN32
