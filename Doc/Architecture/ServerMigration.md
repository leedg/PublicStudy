# 서버 마이그레이션 요약

## 개요
- **목표**: 기존 Windows 전용 구현을 멀티플랫폼 `INetworkEngine` 구조로 전환
- **상태**: 마이그레이션 완료(2026-02-05), 2026-02-09 기준 문서/코드 정합성 갱신

---

## 핵심 변경

### 1) ServerEngine 구조 개편
- `INetworkEngine` + `BaseNetworkEngine`로 공통 로직 분리
- 플랫폼별 엔진 분리:
  - `WindowsNetworkEngine` (IOCP/RIO)
  - `LinuxNetworkEngine` (epoll/io_uring)
  - `macOSNetworkEngine` (kqueue)
- `CreateNetworkEngine("auto")` 팩토리로 플랫폼/버전 자동 선택

### 2) TestServer/TestDBServer 적용
- `IOCPNetworkEngine` 제거 → `INetworkEngine` 사용
- `CreateNetworkEngine("auto")`로 백엔드 선택

---

## 현재 구조 (요약)
```
TestServer / TestDBServer
    ↓
CreateNetworkEngine("auto")
    ↓
INetworkEngine
    ↓
BaseNetworkEngine
    ↓
Windows | Linux | macOS NetworkEngine
    ↓
AsyncIOProvider
```

---

## 현행 상태/주의점
- **Windows** 경로는 실사용/테스트 기준
- **Linux/macOS** 경로는 기능 구현 중(WIP)
- 문서/코드는 `INetworkEngine` 기준으로 유지 중

---

## 참고
- `Doc/Architecture/NetworkArchitecture.md` (현재 아키텍처 설명)
- `Doc/Architecture/MultiplatformEngine.md` (구현 히스토리/상세)

---

**최종 업데이트**: 2026-02-09
