#pragma once

// IOCP-path send buffer pool (singleton), implements IBufferPool.
//          Eliminates per-send heap allocation on Windows IOCP path.
//          Pre-allocates a contiguous slab (poolSize × slotSize bytes) and
//          hands out fixed-size slots via an O(1) free-list stack.
//

#ifdef _WIN32

#include "../../Core/Memory/IBufferPool.h"
#include <cstddef>
#include <malloc.h>
#include <mutex>
#include <numeric>
#include <vector>

namespace Network::Core
{

class SendBufferPool : public ::Network::Core::Memory::IBufferPool
{
  public:
    // Singleton accessor.
    static SendBufferPool &Instance();

    // ─── IBufferPool interface ───────────────────────────────────────────
    bool Initialize(size_t poolSize, size_t slotSize) override;
    void Shutdown() override;

    ::Network::Core::Memory::BufferSlot Acquire() override;
    void                     Release(size_t index) override;

    size_t SlotSize()  const override;
    size_t PoolSize()  const override;
    size_t FreeCount() const override;

    // ─── Concrete helper (stable after Initialize — no lock needed) ──────
    // Get a raw pointer to slot memory by index.
    char *SlotPtr(size_t idx) const;

  private:
    SendBufferPool() = default;
    ~SendBufferPool() override { Shutdown(); }

    void*               mStorage  = nullptr;
    std::vector<size_t> mFreeSlots;
    mutable std::mutex  mMutex;
    size_t              mSlotSize{0};
    size_t              mPoolSize{0};
};

} // namespace Network::Core

#endif // _WIN32
