#pragma once
// English: Unified pre-registered async I/O buffer pool.
//          Supports Windows (_aligned_malloc) and Linux (posix_memalign).
//          Platform-specific allocation is isolated to AllocAligned/FreeAligned.
// 한글: 통합 사전 등록 비동기 I/O 버퍼 풀.
//       Windows(_aligned_malloc)와 Linux(posix_memalign)를 지원.
//       플랫폼별 차이는 AllocAligned/FreeAligned에만 격리.

#include "Interfaces/IBufferPool.h"
#include "Network/Core/AsyncIOProvider.h"

#if defined(_WIN32) || defined(__linux__)
#include <mutex>
#include <numeric>
#include <unordered_map>
#include <vector>

namespace Network
{
namespace AsyncIO
{

class AsyncBufferPool : public IBufferPool
{
  public:
    AsyncBufferPool();
    ~AsyncBufferPool() override;

    AsyncBufferPool(const AsyncBufferPool &) = delete;
    AsyncBufferPool &operator=(const AsyncBufferPool &) = delete;

    bool Initialize(AsyncIOProvider *provider, size_t bufferSize,
                    size_t poolSize) override;
    void Shutdown() override;

    // English: Acquire a free slot. Returns nullptr + outBufferId=-1 if exhausted.
    // 한글: 빈 슬롯 반환. 고갈 시 nullptr + outBufferId=-1.
    uint8_t *Acquire(int64_t &outBufferId) override;
    void Release(int64_t bufferId) override;

    size_t GetBufferSize() const override;
    size_t GetAvailable() const override;
    size_t GetPoolSize() const override;

  private:
    // English: Internal cleanup without acquiring mMutex. Call only while mMutex is held.
    // 한글: mMutex를 잡지 않는 내부 정리 함수. mMutex 보유 상태에서만 호출.
    void ShutdownLocked();

    // English: 4KB page-aligned allocation. Returns nullptr on failure.
    // 한글: 4KB 페이지 정렬 할당. 실패 시 nullptr 반환.
    static uint8_t *AllocAligned(size_t size);
    static void FreeAligned(uint8_t *ptr);

    struct Slot
    {
        uint8_t *ptr = nullptr;
        int64_t bufferId = -1;
        bool inUse = false;
    };

    AsyncIOProvider *mProvider = nullptr;
    std::vector<Slot> mSlots;
    std::vector<size_t> mFreeIndices;                    // O(1) pop/push 프리슬롯 스택
    std::unordered_map<int64_t, size_t> mBufferIdToIndex; // O(1) bufferId → 슬롯 인덱스
    size_t mBufferSize = 0;
    mutable std::mutex mMutex;
};

} // namespace AsyncIO
} // namespace Network
#endif // defined(_WIN32) || defined(__linux__)
