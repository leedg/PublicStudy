#pragma once
// Core/Memory/IOUringBufferPool.h — Linux io_uring fixed buffer pool.
// Use InitializeFixed() to register buffers with a live io_uring instance for
// zero-copy fixed-buffer I/O.  Plain Initialize() skips registration (useful
// for non-fixed-buffer mode or pre-ring allocation).

#include "IBufferPool.h"

#if defined(__linux__)
#include <liburing.h>
#include <mutex>
#include <vector>
#include <sys/uio.h>

namespace Network
{
namespace Core
{
namespace Memory
{

class IOUringBufferPool : public IBufferPool
{
public:
    IOUringBufferPool()  = default;
    ~IOUringBufferPool() override;

    IOUringBufferPool(const IOUringBufferPool&)            = delete;
    IOUringBufferPool& operator=(const IOUringBufferPool&) = delete;

    // Non-fixed mode: allocates slab without io_uring registration.
    bool Initialize(size_t poolSize, size_t slotSize) override;

    // Fixed-buffer mode: allocates slab and calls io_uring_register_buffers.
    // ring is referenced (not owned); caller must keep it alive until Shutdown().
    bool InitializeFixed(io_uring* ring, size_t poolSize, size_t slotSize);

    void Shutdown() override;

    BufferSlot Acquire() override;
    void       Release(size_t index) override;

    size_t SlotSize()  const override { return mSlotSize; }
    size_t PoolSize()  const override { return mPoolSize; }
    size_t FreeCount() const override;

    // io_uring 구체 확장 메서드 — 비가상이므로 구체 타입(IOUringBufferPool)으로 직접 호출.
    // IBufferPool* 경유 시 사용 불가 (의도적 설계: 플랫폼 ABI를 인터페이스에 노출하지 않음).
    int  GetFixedBufferIndex(size_t index) const { return static_cast<int>(index); }
    bool IsFixedBufferMode()               const { return mIsFixed; }

private:
    void*               mStorage  = nullptr; // posix_memalign으로 할당한 슬랩 연속 메모리 (4096 바이트 정렬)
    size_t              mSlotSize = 0;        // 슬롯 하나의 크기 (바이트) — Initialize 이후 불변
    size_t              mPoolSize = 0;        // 전체 슬롯 수 — Initialize 이후 불변
    bool                mIsFixed  = false;    // true이면 io_uring fixed-buffer 모드 (zero-copy I/O 활성화)
    io_uring*           mRing     = nullptr;  // 외부 소유 io_uring 인스턴스 참조 — fixed 모드에서만 유효 (Linux 전용)
    std::vector<iovec>  mIovecs;              // fixed-buffer 등록 시 io_uring_register_buffers에 전달하는 iov 배열
    std::vector<size_t> mFreeIndices;         // 반환된 슬롯 인덱스 스택 (후입선출); mMutex로 보호
    mutable std::mutex  mMutex;               // mStorage, mFreeIndices, mSlotSize, mPoolSize 보호용
};

} // namespace Memory
} // namespace Core
} // namespace Network

#endif // defined(__linux__)
