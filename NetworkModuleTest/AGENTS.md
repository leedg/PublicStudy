# NetworkModuleTest — CLAUDE.md

## 프로젝트 개요

Windows/Linux/macOS 크로스플랫폼 고성능 네트워크 서버 엔진 연구 프로젝트.
IOCP, RIO, epoll, io_uring, kqueue를 추상화한 2계층 비동기 I/O 아키텍처.

**구성요소**:
- `Server/ServerEngine/` — 핵심 네트워크 라이브러리 (공유 lib)
- `Server/TestServer/` — 게임 서버 (클라이언트 연결 처리 + 비동기 DB)
- `Server/DBServer/` — DB 서버
- `Client/TestClient/` — 크로스플랫폼 테스트 클라이언트
- `ModuleTest/` — AsyncIO / DB 저수준 단위 테스트

---

## 아키텍처

### 2계층 구조
```
[고수준] INetworkEngine → BaseNetworkEngine → {Windows/Linux/macOS}NetworkEngine
                ↓ 의존
[저수준] AsyncIOProvider → {Iocp/RIO/Epoll/IOUring/Kqueue}AsyncIOProvider
```
- **고수준**: 세션 생명주기, 이벤트 콜백, Accept/Send 로직
- **저수준**: 버퍼 관리, OS별 비동기 I/O 시스템콜 추상화

### 폴백 체인
- Windows: RIO → IOCP
- Linux: io_uring → epoll
- macOS: kqueue

### 패킷 구조
- **헤더**: `uint16_t size` + `uint16_t id` (4바이트, `#pragma pack(push,1)`)
- 클라이언트↔서버: `PacketHeader` + `PKT_` 접두 구조체
- 서버↔서버: `ServerPacketHeader` (size + id + sequence)
- 인코딩: Little-Endian

---

## 코딩 컨벤션

### 네이밍

| 대상 | 규칙 | 예 |
|------|------|----|
| 클래스/구조체 | PascalCase | `SessionManager`, `GameSession` |
| 함수/메서드 | PascalCase | `Initialize()`, `OnConnected()` |
| 로컬 변수 | camelCase | `clientVersion`, `sessionId` |
| 멤버 변수 | `m` + PascalCase | `mSessions`, `mDBTaskQueue` |
| 상수 | UPPER_CASE | `MAX_PACKET_SIZE`, `DEFAULT_PORT` |
| 패킷 구조체 | `PKT_` + PascalCase | `PKT_PingReq`, `PKT_SessionConnectReq` |
| 인터페이스 | `I` + PascalCase | `INetworkEngine`, `IDatabase` |
| 타입 별칭 | Ref/Type/Id 접미 | `SessionRef`, `ConnectionId` |
| 열거형 원소 | PascalCase | `LogLevel::Debug`, `SessionState::Connected` |

### 네임스페이스
```cpp
namespace Network {
    namespace Core { ... }      // Session, SessionManager
    namespace Utils { ... }     // Logger, Timer, SafeQueue
    namespace TestServer { ... } // GameSession, PacketHandler
}
```

### 주석
영한 병기:
```cpp
// English: Initialize the session
// 한글: 세션 초기화
```

### 파일 구조
- 헤더: `#pragma once`, 전방 선언, 클래스 선언, 타입 정의
- 소스: 헤더 include, 네임스페이스 블록, 구현
- 비trivial 구현은 반드시 .cpp에 분리

---

## 에러 처리

- **네트워크 계층**: 예외 최소화, `bool`/`int` 반환값 기반 처리
- **DB 계층**: `DatabaseException` (std::exception 파생) 사용
- **로깅**: `Logger::Info/Warn/Error/Debug()` 일관 사용
- Windows 에러: `GetLastError()` 결과를 로그에 포함

```cpp
// 올바른 에러 처리 예
if (!session->Initialize(id, socket)) {
    Logger::Error("Session init failed, id: " + std::to_string(id));
    return false;
}
```

---

## 핵심 설계 원칙 (유지 필수)

1. **논블로킹 세션**: `GameSession`에서 DB 작업은 `DBTaskQueue`를 통해 비동기 처리
2. **스레드 안전**: `SafeQueue`, `std::atomic`, `std::mutex` 기반 동기화
3. **팩토리 패턴**: `CreateNetworkEngine()`, `CreateAsyncIOProvider()`로 런타임 선택
4. **Graceful Shutdown**: 세션 전부 종료 → 큐 드레인 → 스레드 Join 순서 엄수
5. **자동 재연결**: 서버↔서버, 클라이언트↔서버 연결 끊김 시 재연결 로직 유지

---

## 빌드

- **언어**: C++17
- **주 개발 환경**: Windows, Visual Studio 2022 (MSVC v143)
- **솔루션**: `NetworkModuleTest.sln` (x64/Debug, x64/Release)
- **CMake**: `Client/TestClient/` 및 `ModuleTest/MultiPlatformNetwork/`
- **주요 링크 라이브러리**: `ServerEngine.lib`, `WS2_32.lib`, `odbc32.lib`

---

## 테스트 실행

```powershell
# 빠른 자동 테스트 (5초)
.\run_test_auto.ps1 -RunSeconds 5

# 대화형 테스트
.\run_test.ps1 -Configuration Debug -ServerPort 9000 -DbPort 8002

# 크래시 재현 테스트
.\run_crash_repro.ps1
```

**프로세스 기동 순서**: TestDBServer(8002) → TestServer(9000) → TestClient

---

## 플랫폼 지원 현황

| 플랫폼 | 상태 | 비고 |
|--------|------|------|
| Windows | ✅ 주 개발 | IOCP(안정) / RIO(고성능) 완전 검증 |
| Linux | ⚠️ 구현됨 | epoll / io_uring 검증 필요 |
| macOS | ⚠️ 구현됨 | kqueue 검증 필요 |
