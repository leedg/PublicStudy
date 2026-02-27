# ConcurrencyRuntime (Common Module)

## 목적
- 서비스 코드에서 raw `atomic/CAS` 직접 사용을 줄이고, 비동기 처리 규약을 공용 모듈로 통일한다.
- 기본 경로는 `Mutex` 백엔드로 두고, 병목 구간에서만 `LockFree` 백엔드를 선택 가능하게 한다.

## 구성
- `ExecutionQueue<T>`: `Mutex`/`LockFree` 선택 가능한 실행 큐
- `Channel<T>`: 타입 기반 메시지 채널
- `KeyedDispatcher`: key affinity 기반 순서 보장 디스패처
- `AsyncScope`: 태스크 추적 + 협력 취소 + drain 대기

## 핵심 정책
- 같은 key는 항상 같은 worker로 라우팅한다.
- `BackpressurePolicy`로 큐 포화 시 행동(`RejectNewest`/`Block`)을 명시한다.
- `Shutdown()` 이후 신규 push를 차단하고, 잔여 큐는 drain 가능하게 유지한다.

## 사용 예시
```cpp
Network::Concurrency::KeyedDispatcher::Options opt;
opt.mName = "GameLogicDispatcher";
opt.mWorkerCount = 8;
opt.mQueueOptions.mBackend = Network::Concurrency::QueueBackend::Mutex;
opt.mQueueOptions.mBackpressure = Network::Concurrency::BackpressurePolicy::Block;
opt.mQueueOptions.mCapacity = 4096;

Network::Concurrency::KeyedDispatcher dispatcher;
dispatcher.Initialize(opt);

dispatcher.Dispatch(sessionId, [this] {
    HandlePacket();
});
```

## 운영 권장
- 기본은 `Mutex`, 병목 구간만 `LockFree`.
- 모니터링 지표: queue depth, rejected count, p99 latency, task failure count.
- 문제 발생 시 백엔드를 설정으로 `Mutex`로 즉시 전환 가능하게 유지.
