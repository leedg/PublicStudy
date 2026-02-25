# AsyncIO Extensions 설계 문서

**작성일**: 2026-02-25
**대상 프로젝트**: ServerEngine
**목표**: CrossPlatform.md 설계의 미구현 항목 구현

---

## 1. 개요

현재 ServerEngine에 구현된 AsyncIOProvider(IOCP/RIO/epoll/io_uring/kqueue)를
보완하기 위해 다음 항목을 추가한다.

| # | 항목 | 위치 |
|---|------|------|
| 1 | `IBufferPool` 인터페이스 + `RIOBufferPool` | `Platforms/Windows/` |
| 2 | `IOUringBufferPool` | `Platforms/Linux/` |
| 3 | `RIOTest.cpp` | `Tests/` |
| 4 | `IOUringTest.cpp` | `Tests/` |
| 5 | `ThroughputBench.cpp`, `LatencyBench.cpp` | `Benchmark/` |

---

## 2. BufferPool 설계

### 2-1. IBufferPool 인터페이스

확장성을 위해 추상 인터페이스를 먼저 정의한다.
나중에 멀티 사이즈 풀 또는 Lock-Free 풀로 교체 시 이 인터페이스만 유지하면 된다.

```cpp
// ServerEngine/Interfaces/IBufferPool.h
class IBufferPool {
public:
    virtual ~IBufferPool() = default;

    virtual bool Initialize(AsyncIOProvider* provider,
                            size_t bufferSize, size_t poolSize) = 0;
    virtual void Shutdown() = 0;

    // Acquire: 빈 버퍼 반환 (nullptr = 풀 고갈)
    virtual uint8_t* Acquire(int64_t& outBufferId) = 0;

    // Release: 버퍼 반환
    virtual void Release(int64_t bufferId) = 0;

    virtual size_t GetBufferSize() const = 0;
    virtual size_t GetAvailable() const = 0;
    virtual size_t GetPoolSize() const = 0;
};
```

### 2-2. RIOBufferPool (Windows)

- `RIOAsyncIOProvider::RegisterBuffer()` 로 N개 버퍼 사전 등록
- 내부: `std::vector<Slot>` + `std::mutex` (단순 풀)
- 각 슬롯: `{ uint8_t* ptr, int64_t bufferId, bool inUse }`

```
Initialize(provider, bufferSize=65536, poolSize=128)
  → 버퍼 N개 malloc + RegisterBuffer → 슬롯 등록

Acquire() → 빈 슬롯 선형 탐색 → inUse=true → (ptr, bufferId) 반환
Release(bufferId) → bufferId로 슬롯 탐색 → inUse=false
Shutdown() → UnregisterBuffer(all) → free(all)
```

### 2-3. IOUringBufferPool (Linux)

- `IOUringAsyncIOProvider::RegisterBuffer()` 로 동일 패턴
- Linux 조건부 컴파일: `#if defined(__linux__) && defined(HAVE_LIBURING)`

---

## 3. 플랫폼별 테스트

### RIOTest.cpp (Windows 전용)

- `#ifdef _WIN32` 가드
- 테스트 항목: Initialize, BufferPool Acquire/Release 루프, Send/Recv 흐름, Shutdown
- GTest 미사용, `std::cout` 기반 (AsyncIOTest.cpp 패턴 동일)

### IOUringTest.cpp (Linux 전용)

- `#if defined(__linux__) && defined(HAVE_LIBURING)` 가드
- 동일 테스트 항목

---

## 4. Benchmark

`ServerEngine/Benchmark/` 폴더에 추가. VS 필터 "Benchmark" 등록.
각 파일에 독립 `main()` 포함 — 실제 네트워크 없이 루프백 소켓 또는
mock 데이터 사용.

### ThroughputBench.cpp

- 측정 지표: 초당 처리 패킷 수, 초당 바이트
- 방법: 고정 메시지 크기로 N초 동안 Send 반복, ProcessCompletions 처리
- 출력: `[BENCH] 10000 packets/sec, 640 MB/sec`

### LatencyBench.cpp

- 측정 지표: 왕복 레이턴시 (min/avg/p99/max, µs)
- 방법: Send → ProcessCompletions 타임스탬프 차이 측정, 1000회 반복
- 출력: `[BENCH] min=12µs avg=18µs p99=45µs max=120µs`

---

## 5. VS 필터 변경

`ServerEngine.vcxproj.filters` 에 추가:

```
Platforms/Windows  → RIOBufferPool.h, RIOBufferPool.cpp
Platforms/Linux    → IOUringBufferPool.h, IOUringBufferPool.cpp
Interfaces         → IBufferPool.h
Tests              → RIOTest.cpp, IOUringTest.cpp
Benchmark          → ThroughputBench.cpp, LatencyBench.cpp (신규 필터)
```

---

## 6. 구현 순서 (커밋 단위)

1. `feat: add IBufferPool interface and RIOBufferPool` — Windows BufferPool
2. `feat: add IOUringBufferPool` — Linux BufferPool
3. `feat: add RIOTest` — Windows 플랫폼 테스트
4. `feat: add IOUringTest` — Linux 플랫폼 테스트
5. `feat: add ThroughputBench and LatencyBench` — 성능 벤치마크
