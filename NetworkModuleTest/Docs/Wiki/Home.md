# NetworkModuleTest

C++ 크로스플랫폼 서버 엔진 — Windows(RIO/IOCP) · Linux(io_uring/epoll) · macOS(kqueue)

## 페이지 목록

| # | 페이지 | 내용 |
|---|--------|------|
| 01 | [전체 구조](01-Overall-Architecture) | 2계층 네트워크 구조, 플랫폼 분기 |
| 02 | [네트워크 엔진](02-Network-Engine) | AsyncIOProvider, 플랫폼 백엔드 |
| 03 | [세션 계층](03-Session-Layer) | Session · SessionManager · SessionPool |
| 04 | [동시성 런타임](04-Concurrency) | ExecutionQueue · KeyedDispatcher · AsyncScope |
| 05 | [데이터베이스](05-Database) | IDatabase · ConnectionPool · 멀티DB |
| 06 | [버퍼/메모리](06-Buffer-Memory) | IBufferPool · RIO/IOUring/Standard |
| 07 | [종료 및 재연결](07-Shutdown-Reconnect) | Graceful Shutdown · DB 재연결 백오프 |
| 08 | [빌드 및 실행](08-Build-and-Run) | 빌드 명령 · 실행 순서 · 포트 정보 |

## 실행 순서

```
TestDBServer (8002) → TestServer (9000) → TestClient
```

## 빌드

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe' `
  'NetworkModuleTest\NetworkModuleTest.sln' /p:Configuration=Debug /p:Platform=x64 /m
```
