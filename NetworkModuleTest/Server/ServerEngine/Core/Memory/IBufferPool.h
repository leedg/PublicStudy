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

};

// Platform-specific helpers are provided as non-virtual concrete methods
// in the derived classes (RIOBufferPool, IOUringBufferPool) only.
// Do NOT add platform #if virtual methods here — use the concrete type directly.

} // namespace Memory
} // namespace Core
