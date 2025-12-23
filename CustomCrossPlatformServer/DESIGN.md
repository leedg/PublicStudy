# Custom Cross-Platform Server Networking Engine (C++)

## 1. 개요 (Overview)

본 프로젝트는 **모단 C++(C++20)** 로 작성된  
**멀티 플로팅 서버 네트워크 엔진**이다.  
게임 서버 및 실시간 백엔드 시스템을 위한 고성능 네트워크 레이어를  
직접 설계·구현하는 것을 목표로 한다.

This project is a **custom-built, cross-platform server networking engine**
written in **modern C++ (C++20)**.
Its primary goal is to design and implement a high-performance networking layer
for game servers and real-time backend systems.

### 해상 설계 필름 (Design Principles)

- 표준 C++ 사용을 최대화하여 이시성과 유지범용성 확분  
- 플랫폼 의엽 코드와 공용 로지의 명표한 분리  
- OS별 최적 I/O 모델을 선택적으로 활용  
- 런타임에 네트워크 백엔드 활성화 / 비활성화 / 교체 가능  
- 명확한 생명주기 관리와 동시성 모델 제공  

---

## 2. 지원 플랫폼 및 네트워크 백엔드  
(Supported Platforms & Backends)

### 2.1 운영체제 (Operating Systems)

- **Windows**
- **Linux**

### 2.2 네트워크 I/O 백엔드 (Network I/O Backends)

| 플랫폼 (Platform) | 백엔드 (Backend) | 설명 (Description) |
|------------------|------------------|-------------------|
| Windows | IOCP | I/O Completion Port 기반 비동기 네트워크 |
| Windows | RIO | Registered I/O 기반 고성능 네트워크 |
| Linux | epoll | Readiness 기반 이벤트 포링 |
| Linux | io_uring | Completion 기반 최신 비동기 I/O |

각 백엔드는 **동분적인 모듈**로 구현되며,  
런타임 설정을 통해 선택적으로 활성할 수 있다.

Each backend is implemented as an **independent module**
and can be selected or disabled **at runtime**.

---

## 3. 설계 목표 (Design Goals)

### 3.1 해상 목표 (Core Goals)

- 플랫폼 간 성늤 저하 없는 멀티 플랫폼 지원  
- 네트워크 코어 로직에서 OS 의엽성 제거  
- 명확하고 예처 가능한 Start / Stop / Shutdown 절차  
- Connection 및 I/O 요청의 명확한 소유권과 수명 관리  
- 행한 전송 계양 확장을 고려한 구조  

### 3.2 범위에서 제제한 항편 (Non-Goals)

- 버뢤 애플리케이션 프레임워크 제공  
- 외부 네트워크 프레임워크 의에별  
- 특정 OS에 과로하게 종소된 최적화  

---

## 4. 전체 아키템테터 (High-Level Architecture)

    +--------------------------------------------------+
    |               Application Layer                  |
    |        (Game Server / Backend Services)          |
    +--------------------------+-----------------------+
                               |
                               v
    +--------------------------------------------------+
    |             Networking Core Layer                |
    |  - Engine lifecycle management                   |
    |  - Connection / Listener management              |
    |  - Unified event dispatch                        |
    |  - Threading & scheduling                        |
    |  - Packet framing & Protobuf integration         |
    +--------------------------+-----------------------+
                               |
                               v
    +--------------------------------------------------+
    |          Transport Backend Abstraction            |
    |              (ITransport Interface)               |
    +--------------------------+-----------------------+
            |                    |                    |
            v                    v                    v
    +--------------+   +----------------+   +----------------+
    | Windows IOCP |   | Windows RIO    |   | Linux Backends |
    |              |   |                |   | epoll / uring |
    +
-------+   +----------------+   +----------------+

---

## 5. 해상 구조 요소 (Core Concepts)

### 5.1 Engine

`Engine`은 네트워크 시스템의 최상위 관리 객체로,  
다음과 같은 책임을 갖다.

- 네트워크 엔진의 전체 생명주기 관리  
- 백엔드 I/O 스레드 생성 및 종료  
- 네트워크 이벤트를 워크 스레드로 전달  
- 런타임 백엔드 교체 제어  

The `Engine` is the top-level object responsible for:
- Managing the entire networking lifecycle
- Starting and stopping backend I/O threads
- Dispatching events to worker threads
- Coordinating backend switching at runtime

**생명주기 (Lifecycle)**

    Create → Start → Running → Stop → Destroy

---

### 5.2 Transport Backend 추약화  
(Transport Backend Abstraction)

모든 플랫폼별 네트워크 구현은 공통 인터페이스를 따른다.

Each platform-specific backend implements a common transport abstraction.

#### 백엔드의 책임 (Responsibilities)

- 소스팅 생성 및 설정  
- Accept / Connect 처리  
- 비동기 Send / Receive 요청  
- I/O 이벤트 수징 및 보고  
- 안전한 종료 및 리소스 정리  

백엔드는 **순수 I/O 처리만 담당**하며,  
애플매이션 로직이나 파크이 해식은 포함하지 않는다.

---

### 5.3 이벤트 모델 (Unified Event Model)

플랫폼별 I/O 모델의 차이를 숨기기 위해  
모든 백엔드는 **공통 이벤트 모델**으로 변환된다.

Supported event types:

- AcceptCompleted  
- ConnectCompleted  
- RecvCompleted  
- SendCompleted  
- ConnectionClosed  
- ErrorOccurred  

이를 통해 네트워크 코어 레이어는  
플랫폼에 완전한 동분성 구조를 유지한다.

---

## 6. 파크스 처리 및 Protocol Buffers  
(Packet Processing & Protocol Buffers)

### 6.1 파크의 구조 (Packet Format)

본 엔진은 **Protocol Buffers**를 기본 멘시지 포막으로 사용한다.

    +----------------+------------------+
    | Length (u32)   | Protobuf Payload |
    +----------------+------------------+

- Length는 네트워크 배이터 오더 사용  
- Payload는 진력화된 Protobuf 멘시지  
- 스트림 기반 재조만 및 분별 수신 지원  

### 6.2 역할 분류 (Responsibility Separation)

- Backend: Raw byte 전송만 담당  
- Core Layer:
  - 스트림 버퍼링
  - 파크 프레이벜
  - Protobuf 진력화 / 역진력화  
- Application:
  - 멘시지 처리 버퍼 

---

## 7. 스레딩 모델 (Threading Model)

### 글바이 구조 (Default Model)

- **I/O Threads**
  - 백엔드별 이벤트 대기 및 수징
- **Worker Threads**
  - 파크을 처리 및 애플마스에이션 코러리 스크립 실행

    [I/O Thread] → Event Queue → [Worker Thread] → Application Logic

이 구조는 I/O 갓을 초진하고  
대수어였어 유동성안 제공한다.

---

## 8. 런타임 백엔드 제어  
(Runtime Backend Control)

### 8.1 백엔드 선택 (Backend Selection)

    {
      "platform": "windows",
      "backend": "rio"
    }

아니면

    {
      "platform": "linux",
      "backend": "io_uring"
    }

### 8.2 백엔드 교체 정책 (Backend Switching Policy)

초기 버전에서는 다음 정책을 사용한다.

- 모든 활성 Connection을 정상 종료  
- 백엔드 I/O 추 Drain  
- 백엔드 완전 종료  
- 신그 백엔드 초기화 및 재시작  

이 방식은 국 복지 보편분 수신을 줄었고  
**안정성과 검증성을 앞에 세웠다.**

---

## 9. 플랫폼 추약화 전략  
(Platform Abstraction Strategy)

### 식 개콜 타임 (Compile-Time)

- `NET_PLATFORM_WINDOWS`
- `NET_PLATFORM_LINUX`

백엔드 기능 플랫:

- `NET_ENABLE_IOCP`
- `NET_ENABLE_RIO`
- `NET_ENABLE_EPOLL`
- `NET_ENABLE_IOURING`

### 런타임 (Runtime)

실제 사용 백엔드는 런타임 설정으로 결정되며,  
하나의 버쏘 버서 매 붕 엠어 지원한다.

---

## 10. 프로젝트 유전조 (Project Structure)

    /net
     ├── include/
     │   └── net/
     │       ├── engine.hpp
     │       ├── transport.hpp
     │       ├── event.hpp
     │       ├── connection.hpp
     │       └── config.hpp
     ├── src/
     │   ├── core/
     │   ├── backend/
     │   │   ├── win_iocp/
     │   │   ├── win_rio/
     │   │   ├── linux_epoll/
     │   │   └── linux_uring/
     │   └── common/
     ├── tests/
     └── CMakeLists.txt

---

## 11. 향후 확장 계획 (Future Work)

- Zero-copy 버퍼 최적화  
- Connection 단위 백엔드 마이기레이션  
- UDP / QUIC 전송 계양 추가  
- 환득 제어 및 화상제어  
- 플런인 기반 백엔드 로딩  

---

## 12. 요약 (Summary)

본 프로젝트는 기존 네트워크 프레임워크에 의사하지 않고,  
**네트워크 엔진의 내부 동작을 직접 설계·영상하는 대 목적이 있다.**

By combining modern C++, multiple OS-level I/O mechanisms,
and a clean separation of responsibilities,
this engine serves both as a **practical networking foundation**
and a **technical exploration of scalable server architecture**.-------
