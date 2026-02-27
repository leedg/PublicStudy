# 01. Overall Architecture

이 페이지는 서버 전체 구성을 한 장으로 보여주고, 어떤 컴포넌트가 어디를 책임지는지 빠르게 정리합니다.

## 정적 이미지 (SVG)
![Overall Architecture](./assets/01-overall-architecture.svg)

## 전체 구조
```mermaid
%%{init: {'theme':'base', 'themeVariables': {
'fontFamily':'Segoe UI, Noto Sans KR, sans-serif',
'primaryColor':'#EAF4FF',
'primaryTextColor':'#0F172A',
'primaryBorderColor':'#3B82F6',
'lineColor':'#64748B',
'secondaryColor':'#FFF8E6',
'tertiaryColor':'#F8FAFC'
}}}%%
flowchart LR
    Client[TestClient]
    DBServer[TestDBServer Optional]

    subgraph TS[TestServer]
        Engine[Client Engine<br/>auto backend]
        SessionMgr[SessionManager]
        ClientSess[ClientSession]
        DBSess[DBServerSession]
        DBQueue[DBTaskQueue<br/>worker=1]
        LocalDB[Local DB<br/>Mock or SQLite]
        DBSocket[DB socket +<br/>recv/ping threads]
    end

    subgraph SE[ServerEngine]
        Core[Network Core]
        Provider[AsyncIOProvider]
        DBModule[Database Module]
    end

    Client <-->|TCP Packet| Engine
    Engine --> SessionMgr --> ClientSess
    ClientSess -->|enqueue| DBQueue --> LocalDB
    DBSess <-->|server packet| DBSocket <-->|TCP| DBServer
    Engine -.-> Core
    Engine -.-> Provider
    DBQueue -.-> DBModule

    classDef external fill:#EEF2FF,stroke:#4F46E5,color:#1E1B4B,stroke-width:1.2px;
    classDef component fill:#E0F2FE,stroke:#0284C7,color:#0C4A6E,stroke-width:1.2px;
    classDef db fill:#ECFDF5,stroke:#059669,color:#064E3B,stroke-width:1.2px;
    classDef infra fill:#FFF7ED,stroke:#C2410C,color:#7C2D12,stroke-width:1.2px;
    class Client,DBServer external;
    class Engine,SessionMgr,ClientSess,DBSess,DBQueue component;
    class LocalDB,DBModule db;
    class Core,Provider,DBSocket infra;
```

## 런타임 백엔드 선택
```mermaid
%%{init: {'theme':'base', 'themeVariables': {
'fontFamily':'Segoe UI, Noto Sans KR, sans-serif',
'lineColor':'#64748B',
'primaryTextColor':'#0F172A'
}}}%%
flowchart TD
    Auto[CreateNetworkEngine auto]
    Win[Windows<br/>RIO -> IOCP]
    Linux[Linux<br/>io_uring -> epoll]
    Mac[macOS<br/>kqueue]

    Auto --> Win
    Auto --> Linux
    Auto --> Mac

    classDef choose fill:#E0F2FE,stroke:#0284C7,color:#0C4A6E,stroke-width:1.2px;
    classDef backend fill:#FFF7ED,stroke:#C2410C,color:#7C2D12,stroke-width:1.2px;
    class Auto choose;
    class Win,Linux,Mac backend;
```

## 한눈에 보기
1. `TestServer`는 클라이언트용 네트워크 엔진과 DB 연동 경로를 함께 관리합니다.
2. 로컬 DB(Mock/SQLite) 경로와 TestDBServer 연동 경로가 공존합니다.
3. DB 관련 동기 작업은 세션 스레드에서 직접 처리하지 않고 `DBTaskQueue`로 넘깁니다.

## 개발자 체크
1. 새 기능 추가 시 먼저 어느 계층(`Session`, `DBTaskQueue`, `DBServerSession`)에 배치할지 결정합니다.
2. 동기 블로킹 코드가 세션 콜백 경로에 들어가지 않도록 확인합니다.
3. 백엔드 선택 로직은 `CreateNetworkEngine("auto")` 동작을 깨지 않게 유지합니다.

## 운영자 체크
1. 클라이언트 지연이 높으면 DBTaskQueue 적체 여부를 먼저 봅니다.
2. DB 서버 연동 장애와 로컬 DB 경로를 분리해 증상을 확인합니다.
3. Windows 경로와 Linux/macOS 경로의 안정성 기대치를 동일하게 두지 않습니다(검증 범위 차이).

## 참고 코드
- `Server/TestServer/src/TestServer.cpp`
- `Server/TestServer/include/TestServer.h`
- `Doc/02_Architecture.md`

검증일: 2026-02-20
