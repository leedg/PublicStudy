#include "StandardBufferPool.h"

#include <limits>
#include <numeric>

#ifdef _WIN32
#include <cstdlib>  // _aligned_malloc, _aligned_free
#elif defined(__linux__) || defined(__APPLE__)
#include <cstdlib>  // posix_memalign, free
#endif

namespace Network
{
namespace Core
{
namespace Memory
{

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

    // 할당 전 size_t 곱셈 오버플로우를 방어한다.
    // 오버플로우 시 poolSize * slotSize 가 묵시적으로 wrap되어 AllocAligned 가
    // 필요보다 훨씬 작은 메모리를 할당하며, 이후 슬롯 오프셋 산술이 힙 경계를 벗어난다.
    if (slotSize > std::numeric_limits<size_t>::max() / poolSize)
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
} // namespace Network
