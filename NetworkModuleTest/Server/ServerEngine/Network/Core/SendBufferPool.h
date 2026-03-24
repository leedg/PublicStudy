#pragma once

// IOCP 경로 전송 버퍼 풀 (싱글턴), IBufferPool 구현.
// Windows IOCP 경로에서 전송마다 발생하는 힙 할당을 제거한다.
// 연속 슬랩(poolSize × slotSize 바이트)을 사전 할당하고,
// O(1) 프리리스트 스택으로 고정 크기 슬롯을 대여·반납한다.

#ifdef _WIN32

#include "../../Core/Memory/IBufferPool.h"
#include <cstddef>
#include <malloc.h>
#include <mutex>
#include <numeric>
#include <vector>

namespace Network::Core
{

class SendBufferPool : public ::Network::Core::Memory::IBufferPool
{
  public:
    // 싱글턴 접근자.
    static SendBufferPool &Instance();

    // ─── IBufferPool interface ───────────────────────────────────────────
    bool Initialize(size_t poolSize, size_t slotSize) override;
    void Shutdown() override;

    ::Network::Core::Memory::BufferSlot Acquire() override;
    void                     Release(size_t index) override;

    size_t SlotSize()  const override;
    size_t PoolSize()  const override;
    size_t FreeCount() const override;

    // ─── Concrete helper (stable after Initialize — no lock needed) ──────
    // 인덱스로 슬롯 메모리 포인터 조회. Initialize 이후 불변 — 락 불필요.
    char *SlotPtr(size_t idx) const;

  private:
    SendBufferPool() = default;
    ~SendBufferPool() override { Shutdown(); }

    void*               mStorage  = nullptr;  // _aligned_malloc 연속 슬랩 (64바이트 정렬, poolSize × slotSize)
    std::vector<size_t> mFreeSlots;            // O(1) 프리슬롯 스택 — back()/pop_back() 대여, push_back() 반납
    mutable std::mutex  mMutex;                // mFreeSlots·mStorage 동시 접근 보호
    size_t              mSlotSize{0};          // 슬롯당 바이트 수 — Initialize 이후 불변
    size_t              mPoolSize{0};          // 전체 슬롯 수 — Initialize 이후 불변
};

} // namespace Network::Core

#endif // _WIN32
