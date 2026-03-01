# 플랫폼별 메모리/버퍼 모듈 설계 전략

> 작성일: 2026-03-01
> 상태: **구현 완료** (2026-03-01)
>
> 구현 결과: `Core/Memory/` 모듈 신규 생성, dead code 전량 제거, RIOAsyncIOProvider 리팩토링 완료.
> 퍼포먼스 검증: 1000/1000 PASS, Errors=0, WS=143.6 MB (이전 193.7 MB 대비 ~50 MB 절감)

---

## 1. 배경 및 목표

### 1.1 현재 상황

현재 코드베이스에는 버퍼 풀 관련 미사용(dead code) 파일이 존재한다:

| 파일 | 상태 | 문제 |
|------|------|------|
| `Platforms/AsyncBufferPool.h/.cpp` | 미사용 | 어떤 Provider도 include하지 않음 |
| `Platforms/Windows/RIOBufferPool.h` | 미사용 | `using RIOBufferPool = AsyncBufferPool;` 별칭만 존재 |
| `Platforms/Linux/IOUringBufferPool.h` | 미사용 | `using IOUringBufferPool = AsyncBufferPool;` 별칭만 존재 |
| `Interfaces/IBufferPool.h` | 미사용 | `AsyncIOProvider*` 의존성 있는 구 인터페이스 |

RIO는 `RIOAsyncIOProvider` 내부 slab pool로 이미 최적화 완료.
IOCP는 `SendBufferPool` 싱글턴으로 send 경로 최적화 완료.
그러나 플랫폼별 버퍼 전략이 각 Provider에 분산되어 있어 독립 모듈로 통합/발전시킬 필요가 있다.

### 1.2 목표

- 플랫폼별 버퍼/메모리 풀 전략을 **하나의 독립 모듈**(`Core/Memory/`)로 정리
- 모든 플랫폼이 단일 `IBufferPool` 인터페이스를 사용
- RIO(zero-copy slab), io_uring(fixed buffers) 등 플랫폼 고유 기법은 별도 구현 클래스로 분리
- **네트워크 모듈에 종속되지 않는** 독립 모듈 — 향후 비-네트워크 컨텐츠에서도 재사용 가능

---

## 2. 플랫폼별 버퍼 전략 분석

### 2.1 세 가지 그룹

```
┌─────────────────────────────────────────────────────────────────────┐
│ Group A — Standard (사전 할당, OS 등록 없음)                         │
│   Windows IOCP, Linux epoll, macOS kqueue                           │
│   특징: 표준 힙 사전 할당, `WSASend`/`send`/`sendto`에 포인터 전달   │
│   버퍼 전략: aligned_malloc 슬롯 풀 + free-list O(1)                │
├─────────────────────────────────────────────────────────────────────┤
│ Group B — RIO (Windows, zero-copy slab)                             │
│   특징: `RIORegisterBuffer` → 단일 RIO_BUFFERID + 슬롯 오프셋       │
│   버퍼 전략: VirtualAlloc slab + 단일 RegisterBuffer 호출           │
│   I/O 제출: RIO_BUF = { BufferId, Offset, Length }                 │
│   복사 절감: recv → slab 직접 수신, 완료 후 memcpy 1회만            │
├─────────────────────────────────────────────────────────────────────┤
│ Group C — io_uring (Linux, fixed buffers)                           │
│   특징: `io_uring_register_buffers` → 고정 버퍼 인덱스              │
│   버퍼 전략: aligned 연속 버퍼 배열 + register_buffers 1회 호출     │
│   I/O 제출: IORING_OP_READ_FIXED/WRITE_FIXED + buf_index           │
│   복사 절감: 커널 직접 접근, user-kernel 복사 최소화                │
└─────────────────────────────────────────────────────────────────────┘
```

### 2.2 현재 RIO 구현 현황 (참고)

`RIOAsyncIOProvider`의 현재 slab pool (이미 동작 중):

```cpp
// 핵심 멤버
void*        mRecvSlab     = nullptr;              // VirtualAlloc recv 슬랩
void*        mSendSlab     = nullptr;              // VirtualAlloc send 슬랩
RIO_BUFFERID mRecvSlabId   = RIO_INVALID_BUFFERID; // 단일 RegisterBuffer
RIO_BUFFERID mSendSlabId   = RIO_INVALID_BUFFERID;
size_t       mSlotSize     = 8192;                 // 슬롯 크기
size_t       mPoolSize     = 0;                    // = maxConcurrent

std::vector<size_t> mFreeRecvSlots;  // O(1) free-list
std::vector<size_t> mFreeSendSlots;
std::unordered_map<SocketHandle, size_t> mSocketRecvSlot;

// I/O 제출 시
RIO_BUF rioBuf;
rioBuf.BufferId = mRecvSlabId;
rioBuf.Offset   = slotIndex * mSlotSize;
rioBuf.Length   = mSlotSize;
```

### 2.3 현재 io_uring 구현 현황 (참고)

`IOUringAsyncIOProvider`는 fixed buffer 지원을 감지하지만 아직 미사용:

```cpp
io_uring mRing;
bool mSupportsFixedBuffers;  // Initialize()에서 감지, 현재 미사용
// PendingOperation: std::unique_ptr<uint8_t[]> mBuffer; (per-op 힙 할당)
```

→ `io_uring_register_buffers` + `IORING_OP_READ_FIXED` 활용 가능성 확인됨.

---

## 3. 설계

### 3.1 디렉토리 구조 (목표)

```
ServerEngine/
└── Core/
    └── Memory/                          ← 신규 독립 모듈
        ├── IBufferPool.h                ← 공용 인터페이스
        ├── StandardBufferPool.h/.cpp    ← Group A: IOCP/epoll/kqueue
        ├── RIOBufferPool.h/.cpp         ← Group B: Windows RIO
        └── IOUringBufferPool.h/.cpp     ← Group C: Linux io_uring
```

기존 미사용 파일 제거 대상:
- `Platforms/AsyncBufferPool.h/.cpp`
- `Platforms/Windows/RIOBufferPool.h`
- `Platforms/Linux/IOUringBufferPool.h`
- `Interfaces/IBufferPool.h` (구 버전)

### 3.2 IBufferPool 인터페이스

```cpp
// Core/Memory/IBufferPool.h
#pragma once
#include <cstdint>
#include <cstddef>

namespace Core::Memory {

// 슬롯 핸들 — 플랫폼 무관한 불투명 핸들
struct BufferSlot {
    void*    ptr      = nullptr;   // 데이터 포인터 (항상 유효)
    size_t   index    = 0;         // 슬롯 인덱스 (내부 관리용)
    size_t   capacity = 0;         // 슬롯 용량
};

class IBufferPool {
public:
    virtual ~IBufferPool() = default;

    // 초기화/해제
    virtual bool Initialize(size_t poolSize, size_t slotSize) = 0;
    virtual void Shutdown() = 0;

    // 슬롯 획득/반환
    virtual BufferSlot Acquire()              = 0;  // nullptr ptr = 풀 소진
    virtual void       Release(size_t index)  = 0;

    // 상태 조회
    virtual size_t SlotSize()  const = 0;
    virtual size_t PoolSize()  const = 0;
    virtual size_t FreeCount() const = 0;

#if defined(_WIN32)
    // RIO 전용 — Group B만 구현, 나머지는 기본값 반환
    virtual uint64_t GetRIOBufferId(size_t /*index*/) const { return UINT64_MAX; }
    virtual size_t   GetRIOOffset  (size_t /*index*/) const { return SIZE_MAX; }
#endif

#if defined(__linux__)
    // io_uring 전용 — Group C만 구현, 나머지는 기본값 반환
    virtual int  GetFixedBufferIndex(size_t /*index*/) const { return -1; }
    virtual bool IsFixedBufferMode()                   const { return false; }
#endif
};

}  // namespace Core::Memory
```

### 3.3 StandardBufferPool (Group A)

대상: IOCP (Windows), epoll (Linux), kqueue (macOS)

**단일 클래스로 통합하는 이유:**
세 플랫폼은 버퍼 풀 로직(사전 할당, O(1) free-list, Acquire/Release)이 완전히 동일하다.
유일한 차이는 할당 함수 1줄(`_aligned_malloc` vs `posix_memalign`)뿐이며,
이는 구현부 내부 `#ifdef` 한 줄로 처리된다.
분리하면 내용이 동일한 복사본 3개가 생겨 유지보수 부담만 증가한다.

```
분리 기준:
  ✓ 동작 방식이 구조적으로 다를 때  → 클래스 분리 (RIO, io_uring)
  ✗ 할당 함수 1줄만 다를 때        → #ifdef 처리 (IOCP/epoll/kqueue)
```

```cpp
// Core/Memory/StandardBufferPool.h
#pragma once
#include "IBufferPool.h"
#include <vector>
#include <mutex>

namespace Core::Memory {

class StandardBufferPool : public IBufferPool {
public:
    bool Initialize(size_t poolSize, size_t slotSize) override;
    void Shutdown() override;

    BufferSlot Acquire() override;
    void       Release(size_t index) override;

    size_t SlotSize()  const override { return mSlotSize; }
    size_t PoolSize()  const override { return mPoolSize; }
    size_t FreeCount() const override;

private:
    // 연속 메모리 블록 (aligned alloc 1회)
    void*  mStorage   = nullptr;
    size_t mSlotSize  = 0;
    size_t mPoolSize  = 0;

    std::vector<size_t> mFreeIndices;   // O(1) 스택 free-list
    mutable std::mutex  mMutex;
};

}  // namespace Core::Memory
```

```cpp
// Core/Memory/StandardBufferPool.cpp — 플랫폼 차이는 여기 1줄만
bool StandardBufferPool::Initialize(size_t poolSize, size_t slotSize) {
#if defined(_WIN32)
    mStorage = _aligned_malloc(poolSize * slotSize, 4096);
#else
    posix_memalign(&mStorage, 4096, poolSize * slotSize);
#endif
    // 이후 free-list 초기화 로직은 세 플랫폼 공통
    ...
}
```

특징:
- `_aligned_malloc` / `posix_memalign`으로 단일 연속 블록 할당 (`#ifdef` 1줄)
- `mFreeIndices` 스택으로 O(1) Acquire/Release
- 플랫폼별 OS 등록 없음 — IOCP/epoll/kqueue 모두 동일

### 3.4 RIOBufferPool (Group B — Windows 전용)

```cpp
// Core/Memory/RIOBufferPool.h  (Windows only)
#pragma once
#if defined(_WIN32)
#include "IBufferPool.h"
#include <winsock2.h>
#include <mswsock.h>
#include <vector>
#include <mutex>

namespace Core::Memory {

class RIOBufferPool : public IBufferPool {
public:
    bool Initialize(size_t poolSize, size_t slotSize) override;
    void Shutdown() override;

    BufferSlot Acquire() override;
    void       Release(size_t index) override;

    size_t SlotSize()  const override { return mSlotSize; }
    size_t PoolSize()  const override { return mPoolSize; }
    size_t FreeCount() const override;

    // IBufferPool RIO 확장 — Provider에서 RIO_BUF 구성 시 사용
    uint64_t GetRIOBufferId(size_t index) const override;
    size_t   GetRIOOffset  (size_t index) const override;

private:
    void*        mSlab     = nullptr;              // VirtualAlloc
    RIO_BUFFERID mSlabId   = RIO_INVALID_BUFFERID; // 단일 RegisterBuffer
    size_t       mSlotSize = 0;
    size_t       mPoolSize = 0;

    std::vector<size_t> mFreeIndices;
    mutable std::mutex  mMutex;

    // RIO 함수 포인터 (로드 on Initialize)
    LPFN_RIOCREATECOMPLETIONQUEUE mPfnRegisterBuffer = nullptr;
    LPFN_RIODEREGISTERBUFFER      mPfnDeregisterBuffer = nullptr;
};

}  // namespace Core::Memory
#endif  // _WIN32
```

핵심 동작:
```
Initialize():
  VirtualAlloc(poolSize × slotSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
  RIORegisterBuffer(slab, totalSize)  → mSlabId  (1회만 호출)
  mFreeIndices = {0, 1, ..., poolSize-1}

Acquire():
  index = mFreeIndices.back(); mFreeIndices.pop_back()
  return { (char*)mSlab + index*mSlotSize, index, mSlotSize }

GetRIOBufferId(index): return (uint64_t)mSlabId
GetRIOOffset  (index): return index * mSlotSize

Shutdown():
  RIODeregisterBuffer(mSlabId)        (1회만 호출)
  VirtualFree(mSlab)
```

Provider 사용 예시:
```cpp
// RIOAsyncIOProvider::RecvAsync()
auto slot = mRecvPool->Acquire();      // IBufferPool 인터페이스
RIO_BUF rioBuf;
rioBuf.BufferId = mRecvPool->GetRIOBufferId(slot.index);
rioBuf.Offset   = mRecvPool->GetRIOOffset(slot.index);
rioBuf.Length   = (ULONG)slot.capacity;
mPfnRIORecv(mRQ, &rioBuf, 1, 0, context);
```

### 3.5 IOUringBufferPool (Group C — Linux 전용)

```cpp
// Core/Memory/IOUringBufferPool.h  (Linux only)
#pragma once
#if defined(__linux__)
#include "IBufferPool.h"
#include <liburing.h>
#include <vector>
#include <mutex>

namespace Core::Memory {

class IOUringBufferPool : public IBufferPool {
public:
    // ring: IOUringAsyncIOProvider의 io_uring*를 받아 register_buffers 호출
    bool Initialize(size_t poolSize, size_t slotSize) override;
    bool InitializeFixed(io_uring* ring, size_t poolSize, size_t slotSize); // 고정 버퍼 모드

    void Shutdown() override;

    BufferSlot Acquire() override;
    void       Release(size_t index) override;

    size_t SlotSize()  const override { return mSlotSize; }
    size_t PoolSize()  const override { return mPoolSize; }
    size_t FreeCount() const override;

    // IBufferPool io_uring 확장
    int  GetFixedBufferIndex(size_t index) const override;  // = (int)index
    bool IsFixedBufferMode()               const override { return mIsFixed; }

private:
    void*  mStorage  = nullptr;   // posix_memalign 단일 블록
    size_t mSlotSize = 0;
    size_t mPoolSize = 0;
    bool   mIsFixed  = false;     // io_uring_register_buffers 호출 여부

    std::vector<iovec>  mIovecs;  // register_buffers에 전달하는 슬롯 목록
    std::vector<size_t> mFreeIndices;
    mutable std::mutex  mMutex;
};

}  // namespace Core::Memory
#endif  // __linux__
```

핵심 동작:
```
InitializeFixed(ring, poolSize, slotSize):
  posix_memalign(&mStorage, 4096, poolSize × slotSize)
  mIovecs[i] = { (char*)mStorage + i*slotSize, slotSize } for each i
  io_uring_register_buffers(ring, mIovecs.data(), poolSize)  → (1회만 호출)
  mIsFixed = true

Acquire():
  index = mFreeIndices.back(); mFreeIndices.pop_back()
  return { (char*)mStorage + index*slotSize, index, mSlotSize }

GetFixedBufferIndex(index): return (int)index

Shutdown():
  if (mIsFixed) io_uring_unregister_buffers(ring)
  free(mStorage)
```

Provider 사용 예시:
```cpp
// IOUringAsyncIOProvider::RecvAsync() — fixed buffer 모드
auto slot = mRecvPool->Acquire();
struct io_uring_sqe* sqe = io_uring_get_sqe(&mRing);
if (mRecvPool->IsFixedBufferMode()) {
    io_uring_prep_read_fixed(sqe, fd, slot.ptr, slot.capacity,
                             0, mRecvPool->GetFixedBufferIndex(slot.index));
} else {
    io_uring_prep_read(sqe, fd, slot.ptr, slot.capacity, 0);
}
```

---

## 4. 구현 범위 및 단계

### 단계 구분

| 단계 | 내용 | 비고 |
|------|------|------|
| **정리 (선행)** | 기존 dead code 제거 | AsyncBufferPool, RIOBufferPool alias, IOUringBufferPool alias, 구 IBufferPool |
| **Phase 1** | `IBufferPool.h` + `StandardBufferPool` 구현 | Group A, 테스트 가능 |
| **Phase 2** | `RIOBufferPool` 구현 + `RIOAsyncIOProvider` 교체 | RIO slab을 외부 풀로 이관 |
| **Phase 3** | `IOUringBufferPool` 구현 + `IOUringAsyncIOProvider` 교체 | fixed buffer 활성화 |

### 현재 우선순위

- RIO는 이미 Provider 내부 slab으로 잘 동작 중 → Phase 2는 우선순위 낮음
- io_uring fixed buffer는 아직 미사용 → Phase 3은 Linux 테스트 환경 준비 후
- **당장 실행할 것**: 기존 dead code 제거 (AsyncBufferPool, alias 파일들)

---

## 5. 미사용 코드 제거 목록

구현 착수 전 제거해야 할 파일:

```
ServerEngine/Platforms/AsyncBufferPool.h
ServerEngine/Platforms/AsyncBufferPool.cpp
ServerEngine/Platforms/Windows/RIOBufferPool.h      (using 별칭만)
ServerEngine/Platforms/Linux/IOUringBufferPool.h    (using 별칭만)
ServerEngine/Interfaces/IBufferPool.h               (구 인터페이스)
```

vcxproj에서 해당 항목도 함께 제거 필요.

---

## 6. 기대 효과

| 항목 | 현재 | 변경 후 |
|------|------|---------|
| 버퍼 풀 위치 | Provider 내부 또는 미사용 dead code | `Core/Memory/` 독립 모듈 |
| 재사용 범위 | 네트워크 전용 | 네트워크 + 비-네트워크 컨텐츠 |
| RIO 버퍼 | Provider 내부 slab (동작 중) | RIOBufferPool 독립 관리 |
| io_uring 버퍼 | per-op 힙 할당 (현재) | fixed buffer O(1) 풀 |
| Standard 버퍼 | 산발적 alloc | StandardBufferPool O(1) 풀 |
| 인터페이스 | 없음 (각 Provider 독자) | IBufferPool 통일 |
