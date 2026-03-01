#if defined(__linux__)
#include "IOUringBufferPool.h"

#include <cstdlib>  // posix_memalign, free
#include <numeric>

namespace Core {
namespace Memory {

IOUringBufferPool::~IOUringBufferPool()
{
    Shutdown();
}

bool IOUringBufferPool::Initialize(size_t poolSize, size_t slotSize)
{
    return InitializeFixed(nullptr, poolSize, slotSize);
}

bool IOUringBufferPool::InitializeFixed(io_uring* ring, size_t poolSize, size_t slotSize)
{
    if (poolSize == 0 || slotSize == 0)
        return false;

    std::lock_guard<std::mutex> lock(mMutex);
    if (mStorage)
        return false; // already initialized

    void* raw = nullptr;
    if (posix_memalign(&raw, 4096, poolSize * slotSize) != 0)
        return false;
    mStorage = raw;

    mSlotSize = slotSize;
    mPoolSize = poolSize;
    mRing     = ring;
    mIsFixed  = (ring != nullptr);

    mIovecs.resize(poolSize);
    for (size_t i = 0; i < poolSize; ++i) {
        mIovecs[i].iov_base = reinterpret_cast<char*>(mStorage) + i * slotSize;
        mIovecs[i].iov_len  = slotSize;
    }

    if (mIsFixed) {
        if (io_uring_register_buffers(mRing, mIovecs.data(),
                                      static_cast<unsigned>(poolSize)) < 0) {
            ::free(mStorage);
            mStorage = nullptr;
            mIovecs.clear();
            return false;
        }
    }

    mFreeIndices.resize(poolSize);
    std::iota(mFreeIndices.begin(), mFreeIndices.end(), size_t(0));

    return true;
}

void IOUringBufferPool::Shutdown()
{
    std::lock_guard<std::mutex> lock(mMutex);

    if (mIsFixed && mRing)
        io_uring_unregister_buffers(mRing);

    if (mStorage) {
        ::free(mStorage);
        mStorage = nullptr;
    }
    mIovecs.clear();
    mFreeIndices.clear();
    mSlotSize = 0;
    mPoolSize = 0;
    mIsFixed  = false;
    mRing     = nullptr;
}

BufferSlot IOUringBufferPool::Acquire()
{
    std::lock_guard<std::mutex> lock(mMutex);
    if (mFreeIndices.empty())
        return {};

    const size_t idx = mFreeIndices.back();
    mFreeIndices.pop_back();
    void* ptr = reinterpret_cast<char*>(mStorage) + idx * mSlotSize;
    return BufferSlot{ptr, idx, mSlotSize};
}

void IOUringBufferPool::Release(size_t index)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mFreeIndices.push_back(index);
}

size_t IOUringBufferPool::FreeCount() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mFreeIndices.size();
}

} // namespace Memory
} // namespace Core

#endif // defined(__linux__)
