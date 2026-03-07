# Apply Efficient Branch to Main — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 코드 리뷰에서 발견된 버그들을 수정한 뒤, 현재 브랜치(`codex/sync-main-local-20260303-180121`)를 `main`에 병합한다.

**Architecture:** 수정은 최소침습적으로 진행한다. 각 버그 수정은 독립 커밋으로 진행하고, 모든 수정 완료 후 main에 merge 한다. 빌드 검증은 Visual Studio MSBuild (x64 Release)로 수행한다.

**Tech Stack:** C++17, Visual Studio 2022, MSBuild, Git

---

## 버그 우선순위

| # | 심각도 | 파일 | 내용 |
|---|--------|------|------|
| 1 | Critical | `ExecutionQueue.h` | `mNotFullCV` 가 두 개의 다른 뮤텍스로 wait() → C++ 표준 위반 |
| 2 | Serious | `KeyedDispatcher.h` | `Dispatch()` 와 `Shutdown()` 사이 TOCTOU: mRunning 통과 후 mWorkers 소멸 |
| 3 | Serious | `AsyncScope.h` | `Reset()` 에 선행 조건 assert 없음 → 잘못된 재사용 시 무음 UB |
| 4 | Style | `IBufferPool.h` | K&R 중괄호, `Core::Memory` 네임스페이스 (Network:: 계층 밖), 한글 주석 없음 |
| 5 | Style | `ExecutionQueue.h` | 모든 줄 뒤 여분 빈 줄 (CRLF 변환 아티팩트) |
| 6 | Docs | `DBTaskQueue.h/.cpp` | `SaveGameProgress`, `Custom` enum 미구현인데 TODO 마커 없음 |

---

### Task 1: ExecutionQueue — mNotFullCV 두 뮤텍스 버그 수정

**문제:** `PushMutexBlocking`은 `mNotFullCV.wait(mMutexQueueMutex)`를, `PushLockFreeBlocking`은 `mNotFullCV.wait(mWaitMutex)`를 사용. 동일 CV에 두 개 뮤텍스 사용은 C++ 표준 위반 (UB + 스레드 영구 블로킹 가능).

**Files:**
- Modify: `Server/ServerEngine/Concurrency/ExecutionQueue.h`

**Step 1: 현재 mNotFullCV 사용 위치 파악**

```bash
grep -n "mNotFullCV" Server/ServerEngine/Concurrency/ExecutionQueue.h
```

두 개의 다른 뮤텍스로 wait하는 라인을 확인한다:
- `mMutexQueueMutex` 보유 중 wait: 뮤텍스 백엔드 경로
- `mWaitMutex` 보유 중 wait: lock-free 백엔드 경로

**Step 2: ExecutionQueue.h 에서 mNotFullCV 선언을 찾아 두 개로 분리**

현재 코드 (단일 CV):
```cpp
std::condition_variable mNotFullCV;
```

수정 후 (두 CV):
```cpp
std::condition_variable mNotFullMutexCV;  // English: Used with mMutexQueueMutex (mutex backend)
                                           // 한글: 뮤텍스 백엔드용 — mMutexQueueMutex와 함께 사용
std::condition_variable mNotFullLFCV;     // English: Used with mWaitMutex (lock-free backend)
                                           // 한글: lock-free 백엔드용 — mWaitMutex와 함께 사용
```

**Step 3: 뮤텍스 백엔드 경로 — mNotFullCV → mNotFullMutexCV**

`PushMutexBlocking` 내의 모든 `mNotFullCV.wait` / `mNotFullCV.wait_until` 을 `mNotFullMutexCV` 로 교체.

`TryPopMutex` 내의 `mNotFullCV.notify_one()` 을 `mNotFullMutexCV.notify_one()` 으로 교체.

**Step 4: lock-free 백엔드 경로 — mNotFullCV → mNotFullLFCV**

`PushLockFreeBlocking` 내의 모든 `mNotFullCV.wait` / `mNotFullCV.wait_until` 을 `mNotFullLFCV` 로 교체.

`TryPopLockFree` 내의 `mNotFullCV.notify_one()` 을 `mNotFullLFCV.notify_one()` 으로 교체.

**Step 5: Shutdown() 내의 notify_all 수정**

`mNotFullCV.notify_all()` 을 두 CV 모두 notify로 교체:
```cpp
mNotFullMutexCV.notify_all();
mNotFullLFCV.notify_all();
```

**Step 6: ExecutionQueue.h 파일에 mNotFullCV 참조가 0개인지 확인**

```bash
grep -c "mNotFullCV[^M^L]" Server/ServerEngine/Concurrency/ExecutionQueue.h
# 결과: 0 이어야 함
```

**Step 7: 커밋**

```bash
git add Server/ServerEngine/Concurrency/ExecutionQueue.h
git commit -m "fix(concurrency): ExecutionQueue mNotFullCV 두 뮤텍스 C++ 표준 위반 수정

뮤텍스 백엔드(mNotFullMutexCV+mMutexQueueMutex)와
lock-free 백엔드(mNotFullLFCV+mWaitMutex)를 위한
condition_variable을 분리하여 표준 위반 해소."
```

---

### Task 2: ExecutionQueue.h — 여분 빈 줄 제거 (CRLF 아티팩트)

**문제:** 파일 전체에 매 코드 줄 뒤에 1-4개의 여분 빈 줄이 삽입됨. 1294 라인 중 논리 코드는 366줄. 가독성 심각 저하.

**Files:**
- Modify: `Server/ServerEngine/Concurrency/ExecutionQueue.h`

**Step 1: 빈 줄 패턴 확인**

```bash
python3 -c "
with open('Server/ServerEngine/Concurrency/ExecutionQueue.h', 'r', encoding='utf-8') as f:
    lines = f.readlines()
print(f'Total lines: {len(lines)}')
blank = sum(1 for l in lines if l.strip() == '')
print(f'Blank lines: {blank}')
print(f'Code lines: {len(lines) - blank}')
"
```

**Step 2: 3개 이상 연속 빈 줄을 1개로 압축**

```python
# cleanup_blank_lines.py (실행 후 삭제)
import re

with open('Server/ServerEngine/Concurrency/ExecutionQueue.h', 'r', encoding='utf-8') as f:
    content = f.read()

# 3개 이상 연속 빈 줄 → 빈 줄 1개
cleaned = re.sub(r'\n{4,}', '\n\n', content)

with open('Server/ServerEngine/Concurrency/ExecutionQueue.h', 'w', encoding='utf-8') as f:
    f.write(cleaned)
```

파일 라인 수가 ~700 이하로 줄었는지 확인.

**Step 3: 커밋**

```bash
git add Server/ServerEngine/Concurrency/ExecutionQueue.h
git commit -m "style: ExecutionQueue.h CRLF 변환 아티팩트 여분 빈 줄 제거"
```

---

### Task 3: KeyedDispatcher — Dispatch/Shutdown TOCTOU 수정

**문제:** `Dispatch()`가 `mRunning == true`를 확인한 뒤, `Shutdown()`이 `mWorkers.clear()`를 완료하면 `mWorkers[workerIndex]` 접근이 UB.

**Files:**
- Modify: `Server/ServerEngine/Concurrency/KeyedDispatcher.h`

**Step 1: 현재 Dispatch 코드 확인**

```bash
grep -n "mWorkers\|mRunning\|clear()" Server/ServerEngine/Concurrency/KeyedDispatcher.h | head -30
```

**Step 2: std::shared_mutex 추가 (C++17)**

`KeyedDispatcher` 멤버 변수 섹션에 추가:
```cpp
#include <shared_mutex>
// ...
mutable std::shared_mutex mWorkersMutex;  // English: Protects mWorkers during Dispatch/Shutdown
                                           // 한글: Dispatch/Shutdown 동시 접근으로부터 mWorkers 보호
```

**Step 3: Dispatch() 에 shared lock 추가**

```cpp
bool Dispatch(uint64_t key, std::function<void()> task, int timeoutMs = -1)
{
    // English: Acquire shared lock first, then check running state under the lock
    //          to prevent TOCTOU race with Shutdown() calling mWorkers.clear().
    // 한글: shared lock 취득 후 running 상태 확인 — Shutdown()의 mWorkers.clear()와의
    //       TOCTOU 경쟁 방지.
    std::shared_lock<std::shared_mutex> sharedLock(mWorkersMutex);

    if (!mRunning.load(std::memory_order_acquire) || !task || mWorkers.empty())
    {
        mRejected.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    const size_t workerIndex = KeyToWorkerIndex(key);
    bool queued = mWorkers[workerIndex]->mQueue.Push(std::move(task), timeoutMs);
    // ...
}
```

**Step 4: Shutdown() 에 exclusive lock 추가 — mWorkers.clear() 전**

```cpp
void Shutdown()
{
    bool expected = true;
    if (!mRunning.compare_exchange_strong(
            expected, false, std::memory_order_acq_rel))
    {
        return;
    }

    // Shutdown queues first (outside the exclusive lock to avoid deadlock with Dispatch's shared lock)
    for (auto &worker : mWorkers)
    {
        worker->mQueue.Shutdown();
    }

    for (auto &worker : mWorkers)
    {
        if (worker->mThread.joinable())
        {
            worker->mThread.join();
        }
    }

    // English: Exclusive lock before clearing mWorkers — prevents Dispatch() from
    //          accessing a partially-destructed vector.
    // 한글: mWorkers.clear() 전 exclusive lock — Dispatch()가 소멸 중인 벡터에 접근하지 못하도록 함.
    {
        std::unique_lock<std::shared_mutex> exclusiveLock(mWorkersMutex);
        mWorkers.clear();
    }

    Utils::Logger::Info(/* ... */);
}
```

**Step 5: 커밋**

```bash
git add Server/ServerEngine/Concurrency/KeyedDispatcher.h
git commit -m "fix(concurrency): KeyedDispatcher Dispatch/Shutdown TOCTOU 수정

std::shared_mutex로 mWorkers 보호:
- Dispatch()는 shared lock으로 mWorkers 읽기
- Shutdown()은 exclusive lock으로 mWorkers.clear() 수행"
```

---

### Task 4: AsyncScope — Reset() 선행 조건 assert 추가

**문제:** `Reset()`은 `mInFlight == 0`일 때만 안전하지만 강제/검증 없음. 잘못된 재사용 시 무음 UB.

**Files:**
- Modify: `Server/ServerEngine/Concurrency/AsyncScope.h`

**Step 1: assert 추가**

```cpp
#include <cassert>
// ...

void Reset()
{
    // English: Precondition: mInFlight MUST be 0. Call WaitForDrain(-1) before Reset().
    //          Violating this precondition corrupts the in-flight counter of the reused scope.
    // 한글: 선행 조건: mInFlight가 반드시 0이어야 함. Reset() 전 WaitForDrain(-1) 호출 필수.
    //       위반 시 재사용 스코프의 in-flight 카운터가 오염됨.
    assert(mInFlight.load(std::memory_order_acquire) == 0 &&
           "AsyncScope::Reset() called while tasks are in-flight");
    mCancelled.store(false, std::memory_order_release);
}
```

**Step 2: 커밋**

```bash
git add Server/ServerEngine/Concurrency/AsyncScope.h
git commit -m "fix(concurrency): AsyncScope::Reset() 선행 조건 assert 추가

mInFlight != 0 상태에서 Reset() 호출 시 즉시 assert 실패.
잘못된 풀 재사용으로 인한 무음 UB를 방지한다."
```

---

### Task 5: IBufferPool.h — 네임스페이스, 중괄호, 한글 주석 수정

**문제:** `Core::Memory` 네임스페이스가 `Network::` 계층 밖에 있음. K&R 중괄호 스타일 사용. 한글 주석 없음.

**Files:**
- Modify: `Server/ServerEngine/Core/Memory/IBufferPool.h`
- Modify: `Server/ServerEngine/Core/Memory/RIOBufferPool.h` (네임스페이스 변경 전파)
- Modify: `Server/ServerEngine/Core/Memory/RIOBufferPool.cpp`
- Modify: `Server/ServerEngine/Core/Memory/IOUringBufferPool.h`
- Modify: `Server/ServerEngine/Core/Memory/IOUringBufferPool.cpp`
- Modify: `Server/ServerEngine/Core/Memory/StandardBufferPool.h`
- Modify: `Server/ServerEngine/Core/Memory/StandardBufferPool.cpp`
- Modify: `Server/ServerEngine/Core/Memory/AsyncBufferPool.h`
- Modify: `Server/ServerEngine/Core/Memory/SendBufferPool.h`
- Modify: `Server/ServerEngine/Core/Memory/SendBufferPool.cpp`

**Step 1: IBufferPool.h 수정 — Allman 중괄호 + Network:: 네임스페이스 + 한글 주석**

```cpp
#pragma once
// English: Core/Memory/IBufferPool.h — Platform-agnostic buffer pool interface.
//          Each concrete pool manages a contiguous slab split into fixed-size slots.
// 한글: 플랫폼 독립적인 버퍼 풀 인터페이스.
//       각 구현체는 고정 크기 슬롯으로 분할된 연속 메모리 슬랩을 관리한다.

#include <cstddef>
#include <cstdint>

namespace Network
{
namespace Core
{
namespace Memory
{

struct BufferSlot
{
    void*  ptr      = nullptr; // English: pointer to slot memory (nullptr → pool exhausted)
                               // 한글: 슬롯 메모리 포인터 (nullptr → 풀 소진)
    size_t index    = 0;       // English: slot index for Release()
                               // 한글: Release() 호출용 슬롯 인덱스
    size_t capacity = 0;       // English: slot size in bytes
                               // 한글: 슬롯 크기 (바이트)
};

class IBufferPool
{
public:
    virtual ~IBufferPool() = default;

    virtual bool Initialize(size_t poolSize, size_t slotSize) = 0;
    virtual void Shutdown() = 0;

    // English: Returns a slot; slot.ptr == nullptr means pool exhausted.
    // 한글: 슬롯 반환; slot.ptr == nullptr 이면 풀 소진.
    virtual BufferSlot Acquire()             = 0;
    virtual void       Release(size_t index) = 0;

    virtual size_t SlotSize()  const = 0;
    virtual size_t PoolSize()  const = 0;
    virtual size_t FreeCount() const = 0;
};

// English: Platform-specific helpers are provided as non-virtual concrete methods
//          in derived classes (RIOBufferPool, IOUringBufferPool) only.
//          Do NOT add platform #if virtual methods here.
// 한글: 플랫폼별 헬퍼는 파생 클래스(RIOBufferPool, IOUringBufferPool)에만
//       비가상 구체 메서드로 제공한다. 여기에 플랫폼 #if 가상 메서드 추가 금지.

} // namespace Memory
} // namespace Core
} // namespace Network
```

**Step 2: 파생 클래스들의 네임스페이스 변경**

`RIOBufferPool.h`, `IOUringBufferPool.h`, `StandardBufferPool.h`, `AsyncBufferPool.h`, `SendBufferPool.h` 에서:
```cpp
// 변경 전:
namespace Core { namespace Memory {

// 변경 후:
namespace Network { namespace Core { namespace Memory {
```
닫는 괄호도 동일하게:
```cpp
// 변경 전:
} // namespace Memory
} // namespace Core

// 변경 후:
} // namespace Memory
} // namespace Core
} // namespace Network
```

**Step 3: 해당 파일들을 #include 하는 코드에서 네임스페이스 참조 변경**

```bash
grep -rn "Core::Memory::\|using namespace Core" Server/ --include="*.h" --include="*.cpp" | grep -v "Network::Core::Memory"
```

발견된 모든 `Core::Memory::` 참조를 `Network::Core::Memory::` 로 교체.

**Step 4: 커밋**

```bash
git add Server/ServerEngine/Core/Memory/
git commit -m "fix(style): IBufferPool 네임스페이스를 Network::Core::Memory로 이전 + Allman 중괄호 + 한글 주석 추가"
```

---

### Task 6: DBTaskQueue — 미구현 enum에 TODO 마커 추가

**문제:** `SaveGameProgress`, `Custom` 이 구현 없이 API로 노출됨. 호출 시 조용히 실패.

**Files:**
- Modify: `Server/TestServer/include/DBTaskQueue.h`
- Modify: `Server/TestServer/src/DBTaskQueue.cpp`

**Step 1: DBTaskQueue.h enum에 TODO 주석 추가**

```cpp
enum class DBTaskType
{
    RecordConnectTime,      // 접속 시간 기록
    RecordDisconnectTime,   // 접속 종료 시간 기록
    UpdatePlayerData,       // 플레이어 데이터 업데이트
    // TODO(#implement): 아래 두 타입은 ProcessTask() 에 핸들러 미구현.
    //                   사용 시 "Unknown DB task type" 오류로 실패함.
    SaveGameProgress,       // 게임 진행 상황 저장 (UNIMPLEMENTED)
    Custom                  // 커스텀 쿼리 (UNIMPLEMENTED)
};
```

**Step 2: DBTaskQueue.cpp ProcessTask() switch default에 명확한 경고 추가**

```cpp
default:
    Utils::Logger::Warn("DBTaskQueue: 미구현 DB 작업 타입 " +
                        std::to_string(static_cast<int>(task.type)) +
                        " — SaveGameProgress/Custom 은 아직 구현되지 않았습니다.");
    result = "Unimplemented DB task type";
    return false;
```

**Step 3: 커밋**

```bash
git add Server/TestServer/include/DBTaskQueue.h Server/TestServer/src/DBTaskQueue.cpp
git commit -m "docs(testserver): DBTaskQueue 미구현 enum에 TODO 마커 및 경고 추가"
```

---

### Task 7: 빌드 검증

**Files:** (없음 — 빌드 스크립트 실행)

**Step 1: MSBuild 경로 확인**

```powershell
# build_all.ps1 의 경로가 현재 환경과 맞는지 확인
Test-Path "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
```

맞지 않으면 올바른 경로로 임시 실행:
```powershell
$msbuild = (Get-Command msbuild -ErrorAction SilentlyContinue)?.Source
if (-not $msbuild) {
    $msbuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
}
& $msbuild "E:\MyGitHub\PublicStudy2\NetworkModuleTest\NetworkModuleTest.sln" /p:Configuration=Release /p:Platform=x64 /nologo /verbosity:minimal /m
```

**Step 2: 빌드 오류 없음 확인**

빌드 출력에서 `error` 가 0개인지 확인:
```bash
# 빌드 로그에서 에러 필터링
... | Select-String -Pattern "error" | Where-Object { $_ -notmatch "0 error" }
```

**Step 3: 에러 발생 시 — 네임스페이스 참조 수정**

Task 5의 네임스페이스 변경으로 인한 컴파일 에러가 발생할 경우:
```bash
grep -rn "Core::Memory::" Server/ --include="*.h" --include="*.cpp"
```
으로 누락된 참조를 찾아 `Network::Core::Memory::` 로 수정.

---

### Task 8: main 브랜치에 병합

**Step 1: 현재 브랜치 상태 확인**

```bash
git status
git log main..HEAD --oneline | wc -l
```

**Step 2: main 체크아웃 후 병합**

```bash
git checkout main
git merge codex/sync-main-local-20260303-180121 --no-ff -m "merge: 효율적인 브랜치 적용 (버그 수정 포함)

- cross-platform async IO (IOCP/RIO/epoll/io_uring/kqueue)
- Concurrency runtime: ExecutionQueue, KeyedDispatcher, AsyncScope
- Memory abstraction: IBufferPool 계층 (Network::Core::Memory)
- Session/Handler 아키텍처 분리: ClientSession, DBTaskQueue
- 성능: atomic 통계 카운터, KeyedDispatcher per-session 순서 보장
- 버그 수정: ExecutionQueue mNotFullCV 이중 뮤텍스, KeyedDispatcher TOCTOU,
            AsyncScope::Reset() 선행 조건 assert"
```

**Step 3: 병합 후 최종 빌드 검증**

Task 7의 빌드 명령 재실행. `0 error(s)` 확인.

**Step 4: 완료 확인**

```bash
git log --oneline -5
git diff HEAD~1..HEAD --stat | tail -5
```

---

## 검증 체크리스트

- [ ] `grep -c "mNotFullCV[^M^L]" ...ExecutionQueue.h` → 0
- [ ] `grep -n "mNotFullMutexCV\|mNotFullLFCV" ...ExecutionQueue.h` → notify/wait 각각 쌍으로 존재
- [ ] `grep -n "shared_mutex" ...KeyedDispatcher.h` → 선언 + Dispatch + Shutdown 사용 확인
- [ ] `grep -n "assert" ...AsyncScope.h` → Reset() 내 assert 존재
- [ ] `grep -n "Network::Core::Memory" Server/ServerEngine/Core/Memory/*.h` → 모든 헤더 적용
- [ ] MSBuild: `0 error(s)`
- [ ] `git log main --oneline -3` → 병합 커밋 확인
