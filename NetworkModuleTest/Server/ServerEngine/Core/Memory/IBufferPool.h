#pragma once
// Core/Memory/IBufferPool.h — Platform-agnostic buffer pool interface.
// Each concrete pool manages a contiguous slab split into fixed-size slots.

#include <cstddef>
#include <cstdint>

namespace Core {
namespace Memory {

struct BufferSlot {
    void*  ptr      = nullptr; // pointer to slot memory (nullptr → pool exhausted)
    size_t index    = 0;       // slot index for Release()
    size_t capacity = 0;       // slot size in bytes
};

class IBufferPool {
public:
    virtual ~IBufferPool() = default;

    virtual bool Initialize(size_t poolSize, size_t slotSize) = 0;
    virtual void Shutdown() = 0;

    virtual BufferSlot Acquire()             = 0; // ptr==nullptr → pool exhausted
    virtual void       Release(size_t index) = 0;

    virtual size_t SlotSize()  const = 0;
    virtual size_t PoolSize()  const = 0;
    virtual size_t FreeCount() const = 0;

#if defined(_WIN32)
    // RIO-specific (only RIOBufferPool overrides; base returns sentinel values)
    virtual uint64_t GetRIOBufferId(size_t /*index*/) const { return UINT64_MAX; }
    virtual size_t   GetRIOOffset  (size_t /*index*/) const { return SIZE_MAX;   }
#endif

#if defined(__linux__)
    // io_uring-specific (only IOUringBufferPool overrides; base returns sentinel values)
    virtual int  GetFixedBufferIndex(size_t /*index*/) const { return -1;    }
    virtual bool IsFixedBufferMode()                   const { return false; }
#endif
};

} // namespace Memory
} // namespace Core
