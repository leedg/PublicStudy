# NetworkModuleTest — AGENTS.md

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

> 상세 내용은 [`Doc/02_Architecture.md`](Doc/02_Architecture.md) 참고.

**핵심 요약**:
- 2계층 구조: 고수준(`INetworkEngine`) + 저수준(`AsyncIOProvider`)
- 폴백 체인: Windows(RIO→IOCP), Linux(io_uring→epoll), macOS(kqueue)
- 패킷: `uint16_t size` + `uint16_t id` 헤더, `#pragma pack(push,1)`, Little-Endian

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

### 중괄호 스타일 (Allman Style)

모든 중괄호는 새 줄에 단독으로 위치한다.

```cpp
// ✅ 올바른 예
void Initialize(int id)
{
    if (id > 0)
    {
        mId = id;
    }
    else
    {
        Logger::Warn("Invalid id");
    }
}

class GameSession : public Session
{
public:
    void OnConnected() override;
};

// ❌ 잘못된 예 (K&R / Egyptian style)
void Initialize(int id) {
    if (id > 0) {
        mId = id;
    }
}
```

적용 범위: 함수, 클래스, 구조체, if/else, for, while, switch, namespace 등 모든 블록.

### 네임스페이스

```cpp
namespace Network {
    namespace Core { ... }       // Session, SessionManager
    namespace Utils { ... }      // Logger, Timer, SafeQueue
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
if (!session->Initialize(id, socket))
{
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

## 코드 수정 워크플로우

### 1. 브랜치 생성

작업 요약을 반영한 이름으로 브랜치를 생성하고 작업한다.

```bash
# 형식: <type>/<brief-summary>
git checkout -b feat/reconnect-backoff
git checkout -b fix/rio-notify-crash
git checkout -b refactor/session-manager-cleanup
```

브랜치 타입: `feat`, `fix`, `refactor`, `docs`, `chore`, `perf`

### 2. 테스트

코드 수정 후 반드시 테스트를 먼저 수행한다.

```powershell
# 빌드 확인 (Debug)
msbuild NetworkModuleTest.sln /p:Configuration=Debug /p:Platform=x64

# 자동 통합 테스트
.\run_test_auto.ps1 -RunSeconds 10

# Graceful shutdown / 크래시 재현 테스트
.\run_crash_repro.ps1
```

**빌드가 깨진 상태로 커밋하지 않는다.**

### 3. Pull Request 및 머지

```bash
# 원격에 브랜치 푸시
git push -u origin <branch-name>

# PR 생성 (gh CLI)
gh pr create --title "<작업 요약>" --base main
```

PR 머지 전 체크리스트:
- [ ] 빌드 성공 (Debug / Release)
- [ ] 통합 테스트 통과
- [ ] 커밋 메시지 컨벤션 준수

### 4. 충돌 해결

충돌 발생 시 rebase를 우선 시도하고, 충돌 내역을 로그에 기록한다.

```bash
# main 최신화 후 rebase
git fetch origin
git rebase origin/main

# 충돌 해결 후
git add <resolved-files>
git rebase --continue

# rebase 불가 시 merge fallback
git merge origin/main
```

충돌이 발생했다면 커밋 메시지에 명시한다:
```
fix/feat/...: <작업 요약>

- 변경 내용 요약
- Merge conflict resolved: <충돌 파일 또는 원인 간략 기술>
```

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
