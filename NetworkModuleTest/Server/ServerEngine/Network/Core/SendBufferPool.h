#pragma once

// English: IOCP-path send buffer pool (singleton).
//          Eliminates per-send heap allocation on Windows IOCP path.
//          Pre-allocates a contiguous slab (poolSize × slotSize bytes) and
//          hands out fixed-size slots via an O(1) free-list stack.
//
// 한글: IOCP 경로 전송 버퍼 풀 (싱글턴).
//       Windows IOCP 경로에서 전송마다 발생하는 힙 할당을 제거한다.
//       연속 슬랩(poolSize × slotSize 바이트)을 사전 할당하고,
//       O(1) 프리리스트 스택으로 고정 크기 슬롯을 대여·반납한다.

#ifdef _WIN32

#include <cstddef>
#include <mutex>
#include <numeric>
#include <vector>

namespace Network::Core
{

class SendBufferPool
{
  public:
    // English: Singleton accessor.
    // 한글: 싱글턴 접근자.
    static SendBufferPool &Instance();

    // English: Initialize the pool. Call once before the first Send().
    //          poolSize  — total number of concurrent send slots
    //          slotSize  — bytes per slot (must be >= SEND_BUFFER_SIZE)
    // 한글: 풀 초기화. 첫 Send() 전에 한 번 호출.
    //       poolSize  — 동시 전송 슬롯 총 개수
    //       slotSize  — 슬롯당 바이트 수 (SEND_BUFFER_SIZE 이상이어야 함)
    void Initialize(size_t poolSize, size_t slotSize);

    // English: Slot handle returned by Acquire().
    //   ptr  — pointer to the slot's memory (nullptr if pool exhausted)
    //   idx  — index used to Release() the slot later
    // 한글: Acquire()가 반환하는 슬롯 핸들.
    //   ptr  — 슬롯 메모리 포인터 (풀 고갈 시 nullptr)
    //   idx  — 나중에 Release()할 때 사용하는 인덱스
    struct Slot
    {
        char  *ptr;
        size_t idx;
    };

    // English: Acquire a free slot from the pool. O(1).
    //          Returns {nullptr, SIZE_MAX} if pool is exhausted.
    // 한글: 풀에서 빈 슬롯 획득. O(1).
    //       풀 고갈 시 {nullptr, SIZE_MAX} 반환.
    Slot Acquire();

    // English: Return a previously acquired slot back to the pool. O(1).
    // 한글: 이전에 획득한 슬롯을 풀에 반납. O(1).
    void Release(size_t slotIdx);

    // English: Get a raw pointer to slot memory by index (no lock needed — stable after Initialize).
    // 한글: 인덱스로 슬롯 메모리 포인터 조회 (Initialize 이후 불변 — 락 불필요).
    char *SlotPtr(size_t idx);

    size_t SlotSize() const { return mSlotSize; }

  private:
    SendBufferPool() = default;

    std::vector<char>   mStorage;    // poolSize × slotSize 연속 메모리
    std::vector<size_t> mFreeSlots;  // O(1) 프리슬롯 스택
    std::mutex          mMutex;
    size_t              mSlotSize{0};
    size_t              mPoolSize{0};
};

} // namespace Network::Core

#endif // _WIN32
