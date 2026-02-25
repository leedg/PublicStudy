#include "Platforms/Linux/IOUringBufferPool.h"

#if defined(__linux__)
#include <cstdlib>

namespace Network
{
namespace AsyncIO
{
namespace Linux
{

IOUringBufferPool::IOUringBufferPool() = default;

IOUringBufferPool::~IOUringBufferPool()
{
    Shutdown();
}

bool IOUringBufferPool::Initialize(AsyncIOProvider *provider, size_t bufferSize,
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
        // English: 4KB page-aligned allocation for optimal io_uring fixed buffer performance.
        // 한글: io_uring fixed buffer 성능 최적화를 위한 4KB 페이지 정렬 할당.
        void *raw = nullptr;
        if (posix_memalign(&raw, 4096, bufferSize) != 0)
        {
            ShutdownLocked();
            return false;
        }

        uint8_t *ptr = static_cast<uint8_t *>(raw);
        int64_t id = provider->RegisterBuffer(ptr, bufferSize);
        if (id < 0)
        {
            free(ptr);
            ShutdownLocked();
            return false;
        }

        mSlots.push_back({ptr, id, false});
    }
    return true;
}

void IOUringBufferPool::Shutdown()
{
    std::lock_guard<std::mutex> lock(mMutex);
    ShutdownLocked();
}

void IOUringBufferPool::ShutdownLocked()
{
    for (auto &slot : mSlots)
    {
        if (slot.ptr)
        {
            if (mProvider && slot.bufferId >= 0)
                mProvider->UnregisterBuffer(slot.bufferId);
            free(slot.ptr);
        }
    }
    mSlots.clear();
    mProvider = nullptr;
    mBufferSize = 0;
}

uint8_t *IOUringBufferPool::Acquire(int64_t &outBufferId)
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

void IOUringBufferPool::Release(int64_t bufferId)
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

size_t IOUringBufferPool::GetBufferSize() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mBufferSize;
}

size_t IOUringBufferPool::GetAvailable() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    size_t count = 0;
    for (const auto &slot : mSlots)
        if (!slot.inUse)
            ++count;
    return count;
}

size_t IOUringBufferPool::GetPoolSize() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mSlots.size();
}

} // namespace Linux
} // namespace AsyncIO
} // namespace Network
#endif // defined(__linux__)
