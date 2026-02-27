#include "Platforms/AsyncBufferPool.h"

#if defined(_WIN32) || defined(__linux__)

#ifdef _WIN32
#include <cstdlib>  // _aligned_malloc, _aligned_free
#elif defined(__linux__)
#include <cstdlib>  // posix_memalign, free
#endif

namespace Network
{
namespace AsyncIO
{

// ---------------------------------------------------------------------------
// Platform-specific allocation
// ---------------------------------------------------------------------------

uint8_t *AsyncBufferPool::AllocAligned(size_t size)
{
#ifdef _WIN32
    return static_cast<uint8_t *>(_aligned_malloc(size, 4096));
#elif defined(__linux__)
    void *raw = nullptr;
    if (posix_memalign(&raw, 4096, size) != 0)
        return nullptr;
    return static_cast<uint8_t *>(raw);
#endif
}

void AsyncBufferPool::FreeAligned(uint8_t *ptr)
{
#ifdef _WIN32
    _aligned_free(ptr);
#elif defined(__linux__)
    ::free(ptr);
#endif
}

// ---------------------------------------------------------------------------
// Common implementation
// ---------------------------------------------------------------------------

AsyncBufferPool::AsyncBufferPool() = default;

AsyncBufferPool::~AsyncBufferPool()
{
    Shutdown();
}

bool AsyncBufferPool::Initialize(AsyncIOProvider *provider, size_t bufferSize,
                                  size_t poolSize)
{
    if (!provider || bufferSize == 0 || poolSize == 0)
        return false;

    std::lock_guard<std::mutex> lock(mMutex);
    mProvider = provider;
    mBufferSize = bufferSize;
    mSlots.reserve(poolSize);

    for (size_t i = 0; i < poolSize; ++i)
    {
        uint8_t *ptr = AllocAligned(bufferSize);
        if (!ptr)
        {
            ShutdownLocked();
            return false;
        }

        int64_t id = provider->RegisterBuffer(ptr, bufferSize);
        if (id < 0)
        {
            FreeAligned(ptr);
            ShutdownLocked();
            return false;
        }

        mSlots.push_back({ptr, id, false});
    }
    return true;
}

void AsyncBufferPool::Shutdown()
{
    std::lock_guard<std::mutex> lock(mMutex);
    ShutdownLocked();
}

void AsyncBufferPool::ShutdownLocked()
{
    for (auto &slot : mSlots)
    {
        if (slot.ptr)
        {
            if (mProvider && slot.bufferId >= 0)
                mProvider->UnregisterBuffer(slot.bufferId);
            FreeAligned(slot.ptr);
        }
    }
    mSlots.clear();
    mProvider = nullptr;
    mBufferSize = 0;
}

uint8_t *AsyncBufferPool::Acquire(int64_t &outBufferId)
{
    std::lock_guard<std::mutex> lock(mMutex);
    for (auto &slot : mSlots)
    {
        if (!slot.inUse)
        {
            slot.inUse = true;
            outBufferId = slot.bufferId;
            return slot.ptr;
        }
    }
    outBufferId = -1;
    return nullptr;
}

void AsyncBufferPool::Release(int64_t bufferId)
{
    std::lock_guard<std::mutex> lock(mMutex);
    for (auto &slot : mSlots)
    {
        if (slot.bufferId == bufferId)
        {
            slot.inUse = false;
            return;
        }
    }
}

size_t AsyncBufferPool::GetBufferSize() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mBufferSize;
}

size_t AsyncBufferPool::GetAvailable() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    size_t count = 0;
    for (const auto &slot : mSlots)
        if (!slot.inUse)
            ++count;
    return count;
}

size_t AsyncBufferPool::GetPoolSize() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mSlots.size();
}

} // namespace AsyncIO
} // namespace Network
#endif // defined(_WIN32) || defined(__linux__)
