# 프로젝트 개요

## 요약
NetworkModuleTest는 ServerEngine을 중심으로 구성된 분산 서버 테스트베드입니다.
현재 기본 런타임 흐름은 TestClient → TestServer → TestDBServer(옵션)이며,
DB 연동은 프로토타입(로그/플레이스홀더 중심) 단계입니다.

## 핵심 구성 요소
- ServerEngine: INetworkEngine/BaseNetworkEngine, AsyncIOProvider, Session/SessionManager, 공용 유틸, DB 모듈(ODBC/OLEDB)
- TestServer: NetworkEngine 기반 테스트 서버, 패킷 수신/처리, DBTaskQueue(비동기 작업)
- TestDBServer: NetworkEngine 기반 DB 서버 프로토타입, Ping/Pong 및 PingTime 기록 (DB 저장은 플레이스홀더)
- TestClient: SessionConnect + Ping/Pong + RTT 통계
- DBServer (DBServer.cpp): AsyncIOProvider 기반 실험/레거시 구현 (기본 실행 경로 아님)
- MultiPlatformNetwork: 보관 | 참고 구현

## 상태 (2026-03-01)
| 모듈 | 상태 | 비고 |
| --- | --- | --- |
| ServerEngine | 진행 중 | Windows x64 Release 1000 클라이언트 PASS 확인 |
| TestServer | 프로토타입 | 세션/핑 처리, DB 옵션(`ENABLE_DATABASE_SUPPORT`) |
| TestDBServer | 프로토타입 | Ping/Pong 및 PingTime 기록, DB 저장은 플레이스홀더 |
| TestClient | 프로토타입 | RTT 통계 포함 |
| MultiPlatformNetwork | 보관 | 참고 구현 |

## 최근 업데이트 (2026-03-01) — Core/Memory 버퍼 모듈 + 핑퐁 검증 페이로드

### Core/Memory 독립 버퍼 모듈
- **Dead code 제거**: `Platforms/AsyncBufferPool.h/.cpp`, `Platforms/Windows/RIOBufferPool.h`, `Platforms/Linux/IOUringBufferPool.h`, `Interfaces/IBufferPool.h` 삭제
- **신규 모듈**: `Core/Memory/` — 플랫폼 독립 버퍼 풀 인터페이스 + 구현 3종
  - `IBufferPool.h` — `BufferSlot{ptr, index, capacity}` 공용 인터페이스 (`namespace Core::Memory`)
  - `StandardBufferPool.h/.cpp` — `_aligned_malloc`/`posix_memalign` 기반, IOCP·epoll·kqueue 공용
  - `RIOBufferPool.h/.cpp` — VirtualAlloc + 1× `RIORegisterBuffer`, `GetSlabId()`/`SlotPtr()` 헬퍼
  - `IOUringBufferPool.h/.cpp` — `posix_memalign` + `io_uring_register_buffers`, `#if __linux__` 전용
- **RIOAsyncIOProvider 리팩토링**: inline slab 멤버 제거 → `Core::Memory::RIOBufferPool mRecvPool, mSendPool` 으로 교체 (락 순서: `mMutex` → 풀 내부 lock, deadlock 없음)

### 핑퐁 검증 페이로드 추가
- Ping 패킷에 **랜덤 숫자 1~5개 (`uint32_t`) + 랜덤 문자 1~5개** 추가 (`mt19937` 생성)
- Pong이 검증 필드를 에코 → `ParsePong()` 에서 원본과 대조 → `GetLastValidationResult()` 반환
- 버퍼 손상 없이 왕복이 정상인지를 매 핑퐁 사이클마다 자동 검증

### 퍼포먼스 테스트 결과 (20260301_163405)
- 1000/1000 PASS, Errors=0, Server WS=**143.6 MB** (이전 193.7 MB → 약 50 MB 감소)
- RTT: avg=2ms, max=20ms @1000 clients

## 이전 업데이트 (2026-03-01 오전) — 메모리 풀 3단계 최적화

- `AsyncBufferPool::Acquire/Release` O(n) 선형 탐색 → O(1) 프리리스트 (`vector<size_t>` 스택 + `unordered_map` 인덱스)
- `Session::ProcessRawRecv()` N 패킷 = N `vector<char>` 힙 할당 → 배치 평탄 버퍼(1 alloc) + 단일 패킷 패스트패스(0 alloc)
- `Session::Send()` IOCP 경로 per-send `vector<char>` alloc 제거 → `SendBufferPool` 싱글턴 O(1) 슬롯 할당
- `Session::PostSend()` IOCP 경로 두 번째 memcpy 제거 → 풀 슬롯 포인터 직접 wsaBuf 설정 (zero-copy WSASend)
- 신규 파일: `Network/Core/SendBufferPool.h/.cpp`

## 이전 업데이트 (2026-02-28) — RIO slab pool / WSA 10055 수정

- `RIOAsyncIOProvider` per-I/O `RIORegisterBuffer` → 사전 등록 slab pool 2개 (recv/send)
- `NetworkTypes.h::MAX_CONNECTIONS` 10000 → 1000 (Non-Paged Pool 한계 대응)
- CQ 깊이 공식 `effectiveMax * 2 + 64`으로 동적 계산
- 결과: 1000 클라이언트 PASS (WSA 10055 해결)

## 이전 업데이트 (2026-02-16) — Session 수신 최적화

- `ProcessRawRecv` O(n) erase → O(1) 오프셋 기반 누적 버퍼 처리 (mRecvAccumOffset)
- `mPingSequence` `uint32_t` → `std::atomic<uint32_t>` (핑 타이머-IO 스레드 경쟁 조건 해소)
- `ClientSession` 의존성 주입: `static sDBTaskQueue` 전역 제거 → 생성자 주입, `MakeClientSessionFactory()` 람다 패턴
- `DBTaskQueue` 워커 수 2 → 1 (세션별 Connect/Disconnect 작업 순서 보장)
- `CloseConnection` 이벤트 경로: `mLogicThreadPool.Submit()` 통일
- `DBPingTimeManager` → `ServerLatencyManager` 통합
- `PlatformDetect.h`: `PLATFORM_WINDOWS/LINUX/MACOS` + `DB_BACKEND_ODBC/OLEDB` 컴파일 타임 매크로 추가
