#include "StandardBufferPool.h"

#include <numeric>

#ifdef _WIN32
#include <cstdlib>  // _aligned_malloc, _aligned_free
#elif defined(__linux__) || defined(__APPLE__)
#include <cstdlib>  // posix_memalign, free
#endif

namespace Core {
namespace Memory {

namespace {

void* AllocAligned(size_t size)
{
#ifdef _WIN32
    return _aligned_malloc(size, 4096);
#elif defined(__linux__) || defined(__APPLE__)
    void* raw = nullptr;
    if (posix_memalign(&raw, 4096, size) != 0)
        return nullptr;
    return raw;
#else
    return nullptr;
#endif
}

void FreeAligned(void* ptr)
{
#ifdef _WIN32
    _aligned_free(ptr);
#elif defined(__linux__) || defined(__APPLE__)
    ::free(ptr);
#else
    (void)ptr;
#endif
}

} // anonymous namespace

StandardBufferPool::~StandardBufferPool()
{
    Shutdown();
}

bool StandardBufferPool::Initialize(size_t poolSize, size_t slotSize)
{
    if (poolSize == 0 || slotSize == 0)
        return false;

    std::lock_guard<std::mutex> lock(mMutex);
    if (mStorage)
        return false; // already initialized

    mStorage = AllocAligned(poolSize * slotSize);
    if (!mStorage)
        return false;

    mSlotSize = slotSize;
    mPoolSize = poolSize;

    mFreeIndices.resize(poolSize);
    std::iota(mFreeIndices.begin(), mFreeIndices.end(), size_t(0));

    return true;
}

void StandardBufferPool::Shutdown()
{
    std::lock_guard<std::mutex> lock(mMutex);
    if (mStorage) {
        FreeAligned(mStorage);
        mStorage = nullptr;
    }
    mFreeIndices.clear();
    mSlotSize = 0;
    mPoolSize = 0;
}

BufferSlot StandardBufferPool::Acquire()
{
    std::lock_guard<std::mutex> lock(mMutex);
    if (mFreeIndices.empty())
        return {};

    const size_t idx = mFreeIndices.back();
    mFreeIndices.pop_back();
    void* ptr = reinterpret_cast<char*>(mStorage) + idx * mSlotSize;
    return BufferSlot{ptr, idx, mSlotSize};
}

void StandardBufferPool::Release(size_t index)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mFreeIndices.push_back(index);
}

size_t StandardBufferPool::FreeCount() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mFreeIndices.size();
}

} // namespace Memory
} // namespace Core
