#include "Platforms/Windows/RIOBufferPool.h"

#ifdef _WIN32
#include <cstdlib>

namespace Network
{
namespace AsyncIO
{
namespace Windows
{

RIOBufferPool::RIOBufferPool() = default;

RIOBufferPool::~RIOBufferPool()
{
    Shutdown();
}

bool RIOBufferPool::Initialize(AsyncIOProvider *provider, size_t bufferSize,
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
        // English: 4KB page-aligned allocation for optimal RIO performance.
        // 한글: RIO 성능 최적화를 위한 4KB 페이지 정렬 할당.
        uint8_t *ptr =
            static_cast<uint8_t *>(_aligned_malloc(bufferSize, 4096));
        if (!ptr)
        {
            ShutdownLocked();
            return false;
        }

        int64_t id = provider->RegisterBuffer(ptr, bufferSize);
        if (id < 0)
        {
            _aligned_free(ptr);
            ShutdownLocked();
            return false;
        }

        mSlots.push_back({ptr, id, false});
    }
    return true;
}

void RIOBufferPool::Shutdown()
{
    std::lock_guard<std::mutex> lock(mMutex);
    ShutdownLocked();
}

void RIOBufferPool::ShutdownLocked()
{
    for (auto &slot : mSlots)
    {
        if (slot.ptr)
        {
            if (mProvider && slot.bufferId >= 0)
                mProvider->UnregisterBuffer(slot.bufferId);
            _aligned_free(slot.ptr);
        }
    }
    mSlots.clear();
    mProvider = nullptr;
    mBufferSize = 0;
}

uint8_t *RIOBufferPool::Acquire(int64_t &outBufferId)
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

void RIOBufferPool::Release(int64_t bufferId)
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

size_t RIOBufferPool::GetBufferSize() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mBufferSize;
}

size_t RIOBufferPool::GetAvailable() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    size_t count = 0;
    for (const auto &slot : mSlots)
        if (!slot.inUse)
            ++count;
    return count;
}

size_t RIOBufferPool::GetPoolSize() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mSlots.size();
}

} // namespace Windows
} // namespace AsyncIO
} // namespace Network
#endif // _WIN32
