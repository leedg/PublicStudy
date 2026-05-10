# ConcurrencyRuntime (Common Module)

## 목적
- 서비스 코드에서 raw `atomic/CAS` 직접 사용을 줄이고, 비동기 처리 규약을 공용 모듈로 통일한다.
- 기본 경로는 `Mutex` 백엔드로 두고, 병목 구간에서만 `LockFree` 백엔드를 선택 가능하게 한다.

## 구성
- `ExecutionQueue<T>`: `Mutex`/`LockFree` 선택 가능한 실행 큐
- `Channel<T>`: 타입 기반 메시지 채널
- `KeyedDispatcher`: key affinity 기반 순서 보장 디스패처
- `AsyncScope`: 태스크 추적 + 협력 취소 + drain 대기
- `TimerQueue`: 단일 배경 스레드 기반 min-heap 타이머 (2026-03-02 신규)

## 핵심 정책
- 같은 key는 항상 같은 worker로 라우팅한다.
- `BackpressurePolicy`로 큐 포화 시 행동(`RejectNewest`/`Block`)을 명시한다.
- `Shutdown()` 이후 신규 push를 차단하고, 잔여 큐는 drain 가능하게 유지한다.
- `TimerQueue::Shutdown()` 시 heap에 남은 미래 항목은 즉시 버리고 worker 스레드를 종료한다.

## ExecutionQueue — 스레드 안전성 설계 (2026-03-06 / 2026-03-10)

### missed-notification race 수정 (2026-03-06)

`Pop()`의 블로킹 대기 경로를 백엔드별로 분리했다.

**문제 원인:** 기존 구현은 백엔드에 상관없이 `mWaitMutex`로 CV를 대기했다. Mutex 백엔드에서 생산자는 `mMutexQueueMutex`를 보유한 채 push + notify하고, 소비자는 `mWaitMutex`로 CV를 대기 등록한다. 두 mutex가 다르므로 "소비자 빈큐 확인 → notify 발생 → 소비자 wait 등록" 순서로 race가 발생하면 notify를 영원히 놓칠 수 있었다 (무한 대기).

**수정:** `Pop()` 내부에서 백엔드에 따라 분기한다.

```
Mutex 백엔드  → PopMutexWait()    : mMutexQueueMutex 하에서 CV 대기
                                    (생산자 push mutex와 동일 → notify slip 불가)
LockFree 백엔드 → PopLockFreeWait() : mWaitMutex 하에서 CV 대기
                                    (TryPushLockFree가 mWaitMutex 보유 중 notify)
```

### CV per-backend 분리 — UB 제거 (2026-03-10)

**문제:** `mNotEmptyCV` 하나를 두 mutex(`mMutexQueueMutex`, `mWaitMutex`)와 함께 `wait()`에 전달하고 있었다. C++ 표준 `[thread.condition.condvar]`은 하나의 `condition_variable`을 항상 동일한 mutex와 써야 한다고 규정한다 — 위반 시 Undefined Behaviour.

**수정:** CV를 백엔드별로 분리.

| CV | mutex | 용도 |
|----|-------|------|
| `mNotEmptyMutexCV` | `mMutexQueueMutex` | Mutex 백엔드 Pop 대기 |
| `mNotEmptyLFCV_pop` | `mWaitMutex` | LockFree 백엔드 Pop 대기 |
| `mNotFullMutexCV` | `mMutexQueueMutex` | Mutex 백엔드 Push 블로킹 대기 |
| `mNotFullLFCV` | `mWaitMutex` | LockFree 백엔드 Push 블로킹 대기 |

`Shutdown()`은 4개 CV 모두 `notify_all()`한다.

## KeyedDispatcher — accessor UB 수정 (2026-03-09)

`GetWorkerCount()` / `GetWorkerQueueSize()`가 락 없이 `mWorkers`를 읽고 있었다. `Shutdown()`이 exclusive lock으로 `mWorkers.clear()`하는 동시에 accessor가 `mWorkers.size()` 또는 `mWorkers[i]`에 접근하면 Undefined Behaviour.

**수정:** 두 accessor에 `std::shared_lock<std::shared_mutex>` 추가.

- 부작용: `Shutdown()` 진행 중 두 함수가 잠깐 block될 수 있다. 정상 운영 중에는 shared lock이므로 reader 간 경합 없음.

## KeyedDispatcher 사용 예시
```cpp
Network::Concurrency::KeyedDispatcher::Options opt;
opt.mName = "GameLogicDispatcher";
opt.mWorkerCount = 8;
opt.mQueueOptions.mBackend = Network::Concurrency::QueueBackend::Mutex;
opt.mQueueOptions.mBackpressure = Network::Concurrency::BackpressurePolicy::Block;
opt.mQueueOptions.mCapacity = 4096;

Network::Concurrency::KeyedDispatcher dispatcher;
dispatcher.Initialize(opt);

// 같은 sessionId → 항상 같은 worker → mRecvMutex 불필요
dispatcher.Dispatch(sessionId, [this] {
    HandlePacket();
});
```

## AsyncScope 주의사항 — 풀 재사용 (2026-03-02)

`AsyncScope`는 `Cancel()` 후 `Reset()`을 명시적으로 호출해야 재사용 가능합니다.

```cpp
// Session 풀 반환 흐름 (SessionPool::ReleaseInternal)
session.Close();        // → mAsyncScope.Cancel() 호출됨
session.Reset();        // → mAsyncScope.Reset() 반드시 호출 필요
// 이후 풀에 슬롯 반환 → 다음 Acquire에서 사용
```

**버그 사례 (2026-03-02 수정):**
- `Session::Reset()`에서 `mAsyncScope.Reset()` 호출 누락
- 결과: 재사용 세션의 `mCancelled=true` 잔존 → `Submit()` 내 `if (!IsCancelled())` 체크로 모든 로직 태스크가 silently skip
- 증상: io_uring 백엔드에서만 `SessionConnectRes` 미수신 (epoll은 첫 실행이라 신선한 슬롯 사용)

**안전 보장:**
- `Reset()` 시점에 `mInFlight == 0` 이 보장됨 (모든 in-flight 람다가 `sessionCopy` shared_ptr를 보유하므로 세션 ref count가 0이 될 때까지 이미 완료됨)

## TimerQueue (2026-03-02 신규)

**파일**: `Concurrency/TimerQueue.h/.cpp`

단발(`ScheduleOnce`) 및 반복(`ScheduleRepeat`) 타이머를 단일 배경 스레드에서 처리하는 min-heap 기반 큐.

```cpp
Network::Concurrency::TimerQueue timerQueue;
timerQueue.Initialize();

// 단발 타이머 (1초 후 1회 실행)
auto h1 = timerQueue.ScheduleOnce([] { DoSomething(); }, 1000);

// 반복 타이머 (5초 간격, false 반환 시 자동 해제)
auto h2 = timerQueue.ScheduleRepeat([this]() -> bool {
    SendDBPing();
    return mRunning.load();  // true = 재등록, false = 자동 해제
}, 5000);

// 취소
timerQueue.Cancel(h2);
timerQueue.Shutdown();
```

**설계 주의사항**:
- 콜백은 worker 스레드에서 실행된다 — 짧게 유지하거나 풀로 오프로드.
- `Shutdown()` 호출 시 아직 실행 안 된 미래 타이머는 모두 버린다 (실행되지 않음).
- `BaseNetworkEngine`과 `TestServer` 양쪽이 각자 `TimerQueue` 멤버를 소유한다.

## 운영 권장
- 기본은 `Mutex`, 병목 구간만 `LockFree`.
- 모니터링 지표: queue depth, rejected count, p99 latency, task failure count.
- 문제 발생 시 백엔드를 설정으로 `Mutex`로 즉시 전환 가능하게 유지.
