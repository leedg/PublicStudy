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
