#pragma once
// Core/Memory/RIOBufferPool.h — Windows RIO pre-registered slab buffer pool.
// Allocates a contiguous slab with VirtualAlloc and registers it once via
// RIORegisterBuffer.  All slots share a single RIO_BUFFERID; per-slot offset
// is index * slotSize.

#include "IBufferPool.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <mutex>
#include <vector>

namespace Network
{
namespace Core
{
namespace Memory
{

class RIOBufferPool : public IBufferPool
{
public:
    RIOBufferPool()  = default;
    ~RIOBufferPool() override;

    RIOBufferPool(const RIOBufferPool&)            = delete;
    RIOBufferPool& operator=(const RIOBufferPool&) = delete;

    // Loads RIO function pointers via a temporary socket, then VirtualAlloc +
    // 1x RIORegisterBuffer for the entire slab.
    bool Initialize(size_t poolSize, size_t slotSize) override;
    void Shutdown() override;

    BufferSlot Acquire() override; // returns {slotPtr, index, slotSize}
    void       Release(size_t index) override;

    // SlotSize/PoolSize are set once in Initialize — safe to read without lock.
    size_t SlotSize()  const override { return mSlotSize; }
    size_t PoolSize()  const override { return mPoolSize; }
    size_t FreeCount() const override;

    // RIO 구체 확장 메서드 — 비가상이므로 구체 타입(RIOBufferPool)으로 직접 호출.
    // IBufferPool* 경유 시 사용 불가 (의도적 설계: 플랫폼 ABI를 인터페이스에 노출하지 않음).
    uint64_t GetRIOBufferId(size_t index) const;
    size_t   GetRIOOffset  (size_t index) const { return index * mSlotSize; }

    // Concrete helpers — used by RIOAsyncIOProvider directly (no virtual dispatch overhead)
    RIO_BUFFERID GetSlabId()           const { return mSlabId; }
    char*        SlotPtr(size_t index) const;

private:
    void*        mSlab    = nullptr;             // VirtualAlloc으로 할당한 슬랩 연속 메모리 (Windows 전용)
    RIO_BUFFERID mSlabId  = RIO_INVALID_BUFFERID; // 슬랩 전체를 대표하는 단일 RIO 버퍼 ID — 슬롯별 offset으로 접근

    size_t       mSlotSize = 0; // 슬롯 하나의 크기 (바이트) — Initialize 이후 불변
    size_t       mPoolSize = 0; // 전체 슬롯 수 — Initialize 이후 불변

    std::vector<size_t> mFreeIndices;  // 반환된 슬롯 인덱스 스택 (후입선출); mMutex로 보호
    mutable std::mutex  mMutex;        // mSlab 접근 외 mFreeIndices/mSlotSize/mPoolSize 보호용

    // RIO 버퍼 등록/해제 함수 포인터 — LoadRIOFunctions()에서 WSAIoctl로 초기화 (Windows RIO 전용)
    LPFN_RIOREGISTERBUFFER   mPfnRegisterBuffer   = nullptr; // RIORegisterBuffer
    LPFN_RIODEREGISTERBUFFER mPfnDeregisterBuffer = nullptr; // RIODeregisterBuffer
};

} // namespace Memory
} // namespace Core
} // namespace Network

#endif // _WIN32
