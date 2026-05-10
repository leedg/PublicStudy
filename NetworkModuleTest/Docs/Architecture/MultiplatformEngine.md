# 멀티플랫폼 NetworkEngine 구현 완료

## 📋 목차
1. [완료된 작업 개요](#완료된-작업-개요)
2. [아키텍처 설명](#아키텍처-설명)
3. [플랫폼별 구현](#플랫폼별-구현)
4. [사용 방법](#사용-방법)
5. [성능 특성](#성능-특성)
6. [해결된 기술적 문제](#해결된-기술적-문제)

---

## 완료된 작업 개요

### ✅ 1단계: 중복 정의 경고 수정
- **문제**: 팩토리 함수 중복 정의로 경고 발생
- **해결**: `NetworkEngineFactory.cpp` 기준으로 정리 및 중복 제거
- **결과**: 경고 없이 깔끔한 빌드 성공

### ✅ 4단계: LinuxNetworkEngine 구현
- **파일**:
  - `Network/Platforms/LinuxNetworkEngine.h`
  - `Network/Platforms/LinuxNetworkEngine.cpp`
- **기능**:
  - epoll 모드 지원 (모든 Linux 버전)
  - io_uring 모드 지원 (Linux 5.1+)
  - 자동 OS 버전 감지 및 최적 백엔드 선택

### ✅ 5단계: macOSNetworkEngine 구현
- **파일**:
  - `Network/Platforms/macOSNetworkEngine.h`
  - `Network/Platforms/macOSNetworkEngine.cpp`
- **기능**:
  - kqueue 기반 고성능 이벤트 알림
  - macOS 최적화

### ✅ NetworkEngineFactory 업데이트
- Windows, Linux, macOS 모든 플랫폼 지원
- 자동 감지 로직 구현
- 명시적 백엔드 선택 지원

---

## 아키텍처 설명

### 계층 구조
```
┌─────────────────────────────────────────────┐
│          사용자 코드 (Application)           │
└─────────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────────┐
│      INetworkEngine (인터페이스)             │
│  - Initialize(), Start(), Stop()            │
│  - SendData(), CloseConnection()            │
│  - RegisterEventCallback()                  │
└─────────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────────┐
│      BaseNetworkEngine (공통 구현)           │
│  - Session 관리                             │
│  - 이벤트 시스템                            │
│  - 로직 스레드 풀                           │
│  - 통계 수집                                │
└─────────────────────────────────────────────┘
                    ↓
      ┌─────────────┴─────────────┐
      ↓                           ↓
┌──────────────┐          ┌──────────────┐
│   Windows    │          │    Linux     │
│Network Engine│          │Network Engine│
│              │          │              │
│ - IOCP       │          │ - epoll      │
│ - RIO        │          │ - io_uring   │
└──────────────┘          └──────────────┘
      ↓                           ↓
┌──────────────┐          ┌──────────────┐
│AsyncIOProvider│         │AsyncIOProvider│
│              │          │              │
│WindowsIOCP   │          │ Epoll        │
│WindowsRIO    │          │ IOUring      │
└──────────────┘          └──────────────┘

      ┌──────────────┐
      │    macOS     │
      │Network Engine│
      │              │
      │ - kqueue     │
      └──────────────┘
            ↓
      ┌──────────────┐
      │AsyncIOProvider│
      │              │
      │  Kqueue      │
      └──────────────┘
```

### 책임 분리

#### BaseNetworkEngine (공통 로직)
- **Session 관리**: SessionManager와 연동하여 연결 생성/삭제
- **이벤트 시스템**: Connected, Disconnected, DataReceived, DataSent 콜백
- **스레드 풀**: 비즈니스 로직을 별도 스레드에서 처리
- **통계 수집**: 연결 수, 송수신 바이트, 에러 카운트

#### Platform-Specific Engine (플랫폼 로직)
- **Accept 루프**: 새 연결 수락
- **Worker 스레드**: I/O 완료 이벤트 처리
- **플랫폼 초기화**: 소켓 생성, Provider 설정
- **Provider 연동**: AsyncIOProvider를 통한 실제 I/O

#### AsyncIOProvider (저수준 I/O)
- **추상화 계층**: 플랫폼별 I/O API 통일
- **버퍼 관리**: 등록/해제
- **비동기 작업**: SendAsync, RecvAsync
- **완료 처리**: ProcessCompletions

---

## 플랫폼별 구현

### Windows

#### IOCP (I/O Completion Port)
```cpp
auto engine = CreateNetworkEngine("iocp");
```
- **지원 버전**: 모든 Windows 버전
- **특징**:
  - 안정적이고 검증된 기술
  - 커널 모드에서 효율적인 스레드 스케줄링
  - 대규모 연결 처리에 적합

#### RIO (Registered I/O)
```cpp
auto engine = CreateNetworkEngine("rio");
```
- **지원 버전**: Windows 8+
- **특징**:
  - 더 높은 성능 (CPU 오버헤드 감소)
  - 버퍼 사전 등록으로 메모리 복사 최소화
  - 고성능 서버에 최적

#### 자동 선택
```cpp
auto engine = CreateNetworkEngine("auto");
```
- Windows 8+ → RIO
- Windows 7 이하 → IOCP

### Linux

#### epoll
```cpp
auto engine = CreateNetworkEngine("epoll");
```
- **지원 버전**: 모든 Linux 버전
- **특징**:
  - 표준 Linux 이벤트 알림 메커니즘
  - O(1) 복잡도
  - 대규모 파일 디스크립터 처리

#### io_uring
```cpp
auto engine = CreateNetworkEngine("io_uring");
```
- **지원 버전**: Linux 5.1+
- **특징**:
  - 최신 비동기 I/O 인터페이스
  - 시스템 콜 오버헤드 최소화
  - Ring buffer 기반 고성능

#### 자동 선택
```cpp
auto engine = CreateNetworkEngine("auto");
```
- Linux 5.1+ → io_uring
- Linux 5.0 이하 → epoll

### macOS

#### kqueue
```cpp
auto engine = CreateNetworkEngine("kqueue");  // 또는 "auto"
```
- **지원 버전**: 모든 macOS 버전
- **특징**:
  - BSD 계열 표준 이벤트 알림
  - 다양한 이벤트 소스 통합 모니터링
  - 파일, 소켓, 시그널 등 통일된 인터페이스

---

## 사용 방법

### 기본 사용 예제

```cpp
#include "Network/Core/NetworkEngine.h"

using namespace Network::Core;

int main()
{
    // 1. 엔진 생성 (자동 감지)
    auto engine = CreateNetworkEngine("auto");
    if (!engine)
    {
        return -1;
    }

    // 2. 이벤트 핸들러 등록
    engine->RegisterEventCallback(
        NetworkEvent::Connected,
        [](const NetworkEventData& data) {
            std::cout << "Client connected: " << data.connectionId << std::endl;
        });

    engine->RegisterEventCallback(
        NetworkEvent::DataReceived,
        [&engine](const NetworkEventData& data) {
            // Echo 서버 예제
            engine->SendData(data.connectionId, data.data.get(), data.dataSize);
        });

    engine->RegisterEventCallback(
        NetworkEvent::Disconnected,
        [](const NetworkEventData& data) {
            std::cout << "Client disconnected: " << data.connectionId << std::endl;
        });

    // 3. 초기화 및 시작
    if (!engine->Initialize(1000, 8080))
    {
        return -1;
    }

    if (!engine->Start())
    {
        return -1;
    }

    // 4. 서버 실행
    std::cout << "Server running on port 8080..." << std::endl;
    std::cout << "Press Enter to stop..." << std::endl;
    std::cin.get();

    // 5. 종료
    engine->Stop();

    return 0;
}
```

### 플랫폼별 명시적 선택

```cpp
// Windows에서 RIO 강제 사용
#ifdef _WIN32
    auto engine = CreateNetworkEngine("rio");
#endif

// Linux에서 io_uring 강제 사용
#ifdef __linux__
    auto engine = CreateNetworkEngine("io_uring");
#endif

// macOS에서 kqueue 사용
#ifdef __APPLE__
    auto engine = CreateNetworkEngine("kqueue");
#endif
```

### 사용 가능한 엔진 타입 조회

```cpp
auto types = GetAvailableEngineTypes();
for (const auto& type : types)
{
    std::cout << "Available: " << type << std::endl;
}
```

---

## 성능 특성

### Windows

| 백엔드 | 처리량 | 레이턴시 | CPU 사용률 | 메모리 |
|--------|--------|----------|------------|--------|
| IOCP   | ★★★★☆  | ★★★★☆    | ★★★★☆      | ★★★★☆  |
| RIO    | ★★★★★  | ★★★★★    | ★★★★★      | ★★★★☆  |

**권장사항**:
- 일반적인 서버: IOCP (안정성 우선)
- 고성능 서버: RIO (처리량 우선)

### Linux

| 백엔드    | 처리량 | 레이턴시 | CPU 사용률 | 메모리 |
|-----------|--------|----------|------------|--------|
| epoll     | ★★★★☆  | ★★★★☆    | ★★★★☆      | ★★★★☆  |
| io_uring  | ★★★★★  | ★★★★★    | ★★★★★      | ★★★★☆  |

**권장사항**:
- Linux 5.0 이하: epoll (유일한 선택)
- Linux 5.1+: io_uring (최신 커널에서 최고 성능)

### macOS

| 백엔드  | 처리량 | 레이턴시 | CPU 사용률 | 메모리 |
|---------|--------|----------|------------|--------|
| kqueue  | ★★★★☆  | ★★★★☆    | ★★★★☆      | ★★★★☆  |

---

## 해결된 기술적 문제

### 1. 이름 충돌 문제 (Name Collision)

#### 문제 상황
```cpp
class AsyncIOProvider {
    virtual const char* GetLastError() const = 0;  // 에러 메시지 반환
};

// Windows API
DWORD GetLastError();  // 에러 코드 반환
```

Provider 클래스 내부에서 `GetLastError()`를 호출하면:
- 컴파일러는 **멤버 함수**를 먼저 찾음
- Windows API 함수 대신 클래스 메서드가 호출됨
- 타입 불일치 발생: `const char*` vs `DWORD`

#### 에러 메시지
```
error C2440: '초기화 중': 'const char *'에서 'DWORD'(으)로 변환할 수 없습니다.
```

#### 해결 방법
```cpp
// ❌ 잘못된 코드
DWORD error = GetLastError();

// ✅ 올바른 코드
DWORD error = ::GetLastError();  // :: = 전역 스코프
```

**`::`를 사용하면** 명시적으로 전역 함수를 호출합니다.

### 2. WSAGetLastError 치환 문제

#### 문제
전역 치환 시 `WSAGetLastError()` → `WSA::GetLastError()`로 잘못 변경됨

#### 해결
```cpp
// 1단계: GetLastError() → ::GetLastError()
// 2단계: WSA::GetLastError() → WSAGetLastError() (수정)
```

### 3. 함수 정의 이름 충돌

#### 문제
```cpp
// GetLastError() 함수 구현 자체도 치환됨
const char* IocpAsyncIOProvider::::GetLastError() const  // 이중 콜론!
```

#### 해결
클래스 멤버 함수 정의는 개별적으로 수정

---

## 디렉토리 구조

```
ServerEngine/
├── Network/
│   ├── Core/
│   │   ├── NetworkEngine.h              // 인터페이스
│   │   ├── BaseNetworkEngine.h/.cpp     // 공통 구현
│   │   ├── NetworkEngineFactory.cpp     // 팩토리 함수
│   │   ├── AsyncIOProvider.h            // I/O 추상화
│   │   └── ...
│   └── Platforms/
│       ├── WindowsNetworkEngine.h/.cpp  // Windows 구현
│       ├── LinuxNetworkEngine.h/.cpp    // Linux 구현
│       └── macOSNetworkEngine.h/.cpp    // macOS 구현
└── Platforms/
    ├── Windows/
    │   ├── WindowsIOCPProvider.cpp      // IOCP 구현
    │   └── WindowsRIOProvider.cpp       // RIO 구현
    ├── Linux/
    │   ├── EpollAsyncIOProvider.cpp     // epoll 구현
    │   └── IOUringAsyncIOProvider.cpp   // io_uring 구현
    └── macOS/
        └── KqueueAsyncIOProvider.cpp    // kqueue 구현
```

---

## 빌드 결과

### Windows (Visual Studio 2022)
```
✅ ServerEngine.lib 생성 성공
✅ 경고 없음
✅ 에러 없음
```

### 컴파일된 파일
- BaseNetworkEngine.cpp
- NetworkEngineFactory.cpp
- WindowsNetworkEngine.cpp
- LinuxNetworkEngine.cpp
- macOSNetworkEngine.cpp
- WindowsIOCPProvider.cpp
- WindowsRIOProvider.cpp
- 기타 Provider 구현들

---

## 다음 단계 (선택사항)

### 1. 성능 벤치마크
- IOCP vs RIO 성능 비교
- epoll vs io_uring 성능 비교
- 플랫폼 간 성능 비교

### 2. TestServer 통합
- 실제 서버 애플리케이션에서 테스트
- 부하 테스트 수행
- 안정성 검증

### 3. 고급 기능 추가
- SSL/TLS 지원
- HTTP/WebSocket 프로토콜 레이어
- Connection pooling
- Rate limiting

### 4. 문서화
- API 레퍼런스 문서
- 튜토리얼 작성
- 성능 튜닝 가이드

---

## 라이선스 및 기여

이 구현은 학습 및 연구 목적으로 작성되었습니다.

**주요 기여자**:
- BaseNetworkEngine 설계 및 구현
- 멀티플랫폼 지원 아키텍처
- 자동 백엔드 선택 로직

---

## 참고 자료

### Windows
- [IOCP Documentation](https://docs.microsoft.com/en-us/windows/win32/fileio/i-o-completion-ports)
- [RIO Documentation](https://docs.microsoft.com/en-us/windows/win32/winsock/winsock-registered-i-o-rio-extensions)

### Linux
- [epoll man page](https://man7.org/linux/man-pages/man7/epoll.7.html)
- [io_uring Documentation](https://kernel.dk/io_uring.pdf)

### macOS
- [kqueue man page](https://www.freebsd.org/cgi/man.cgi?kqueue)

---

**구현 완료일**: 2026-02-05
**빌드 환경**: Visual Studio 2022, Windows 10, x64 Debug / Ubuntu 22.04 Docker (gcc-12)
**언어 표준**: C++17

---

## 업데이트 이력

### 2026-03-02 — 비동기 고도화 + Linux Docker 검증

**LinuxNetworkEngine 수정:**
- `mLogicThreadPool.Submit` → `mLogicDispatcher.Dispatch(sessionId, lambda)` 교체
- io_uring 초기화 실패 시 epoll 폴백 추가 (`InitializePlatform()`)
- `EpollAsyncIOProvider`: `Logger::Error` → `Utils::Logger::Error` 네임스페이스 수정

**BaseNetworkEngine 공통 변경:**
- `mLogicThreadPool` → `KeyedDispatcher mLogicDispatcher` (key-affinity 직렬화)
- `TimerQueue mTimerQueue` 추가 (세션 타임아웃, DB 핑 타이머)
- `NetworkEventBus::Publish()` 연동 (다중 구독자 이벤트)

**AsyncScope 풀 재사용 버그 수정:**
- `Session::Close()` → `mAsyncScope.Cancel()` (기존)
- `Session::Reset()` → `mAsyncScope.Reset()` **추가** (풀 재사용 전 초기화)
- 미수정 시: io_uring처럼 두 번째 이후 세션에서 모든 로직 태스크 skip

**Linux Docker 통합 테스트 결과:**
- 환경: Ubuntu 22.04, gcc-12, kernel 6.6.87.2-WSL2
- epoll + io_uring 양 백엔드 10 클라이언트 5 핑 **PASS**
- 로그: `Docs/Performance/Logs/20260302_191739_linux/`
