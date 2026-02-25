#pragma once
// English: io_uring pre-registered buffer pool for Linux.
//          Implements IBufferPool using IOUringAsyncIOProvider::RegisterBuffer.
// 한글: Linux io_uring 사전 등록 버퍼 풀.
//       IOUringAsyncIOProvider::RegisterBuffer를 사용해 IBufferPool 구현.

#include "Interfaces/IBufferPool.h"
#include "Network/Core/AsyncIOProvider.h"

#if defined(__linux__)
#include <mutex>
#include <vector>

namespace Network
{
namespace AsyncIO
{
namespace Linux
{

class IOUringBufferPool : public IBufferPool
{
  public:
    IOUringBufferPool();
    ~IOUringBufferPool() override;

    IOUringBufferPool(const IOUringBufferPool &) = delete;
    IOUringBufferPool &operator=(const IOUringBufferPool &) = delete;

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

    struct Slot
    {
        uint8_t *ptr = nullptr;
        int64_t bufferId = -1;
        bool inUse = false;
    };

    AsyncIOProvider *mProvider = nullptr;
    std::vector<Slot> mSlots;
    size_t mBufferSize = 0;
    mutable std::mutex mMutex;
};

} // namespace Linux
} // namespace AsyncIO
} // namespace Network
#endif // defined(__linux__)
