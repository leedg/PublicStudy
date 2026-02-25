#pragma once
// English: RIO pre-registered buffer pool for Windows.
//          Implements IBufferPool using AsyncIOProvider::RegisterBuffer.
// 한글: Windows RIO 사전 등록 버퍼 풀.
//       AsyncIOProvider::RegisterBuffer를 사용해 IBufferPool 구현.

#include "Interfaces/IBufferPool.h"
#include "Network/Core/AsyncIOProvider.h"

#ifdef _WIN32
#include <mutex>
#include <vector>

namespace Network
{
namespace AsyncIO
{
namespace Windows
{

class RIOBufferPool : public IBufferPool
{
  public:
    RIOBufferPool();
    ~RIOBufferPool() override;

    RIOBufferPool(const RIOBufferPool &) = delete;
    RIOBufferPool &operator=(const RIOBufferPool &) = delete;

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

} // namespace Windows
} // namespace AsyncIO
} // namespace Network
#endif // _WIN32
