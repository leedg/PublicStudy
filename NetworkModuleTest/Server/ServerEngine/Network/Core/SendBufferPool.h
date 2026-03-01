#pragma once

// English: IOCP-path send buffer pool (singleton), implements IBufferPool.
//          Eliminates per-send heap allocation on Windows IOCP path.
//          Pre-allocates a contiguous slab (poolSize × slotSize bytes) and
//          hands out fixed-size slots via an O(1) free-list stack.
//
// 한글: IOCP 경로 전송 버퍼 풀 (싱글턴), IBufferPool 구현.
//       Windows IOCP 경로에서 전송마다 발생하는 힙 할당을 제거한다.
//       연속 슬랩(poolSize × slotSize 바이트)을 사전 할당하고,
//       O(1) 프리리스트 스택으로 고정 크기 슬롯을 대여·반납한다.

#ifdef _WIN32

#include "../../Core/Memory/IBufferPool.h"
#include <cstddef>
#include <malloc.h>
#include <mutex>
#include <numeric>
#include <vector>

namespace Network::Core
{

class SendBufferPool : public ::Core::Memory::IBufferPool
{
  public:
    // English: Singleton accessor.
    // 한글: 싱글턴 접근자.
    static SendBufferPool &Instance();

    // ─── IBufferPool interface ───────────────────────────────────────────
    bool Initialize(size_t poolSize, size_t slotSize) override;
    void Shutdown() override;

    ::Core::Memory::BufferSlot Acquire() override;
    void                     Release(size_t index) override;

    size_t SlotSize()  const override;
    size_t PoolSize()  const override;
    size_t FreeCount() const override;

    // ─── Concrete helper (stable after Initialize — no lock needed) ──────
    // English: Get a raw pointer to slot memory by index.
    // 한글: 인덱스로 슬롯 메모리 포인터 조회 (Initialize 이후 불변 — 락 불필요).
    char *SlotPtr(size_t idx) const;

  private:
    SendBufferPool() = default;
    ~SendBufferPool() override { Shutdown(); }

    void*               mStorage  = nullptr;  // _aligned_malloc 연속 메모리
    std::vector<size_t> mFreeSlots;            // O(1) 프리슬롯 스택
    mutable std::mutex  mMutex;
    size_t              mSlotSize{0};
    size_t              mPoolSize{0};
};

} // namespace Network::Core

#endif // _WIN32
