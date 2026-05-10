# Wiki 소스 일원화 및 GitHub Wiki 자동 발행 구현 플랜

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `Doc/WikiDraft/`와 `Doc/Reports/WikiPackage/`를 `Doc/Wiki/`로 통합하고, 현재 코드 기준으로 8페이지 Wiki를 재작성한 뒤 main push 시 GitHub Wiki에 자동 동기화한다.

**Architecture:** `Doc/Wiki/`가 단일 소스. `.mmd` Mermaid 소스 → `assets/` SVG/PNG 렌더링. `pre-push` git hook이 `publish-wiki.ps1`을 호출해 `leedg/PublicStudy.wiki.git`에 동기화.

**Tech Stack:** Markdown, Mermaid, PowerShell, bash (git hook), GitHub Wiki (git repo)

**Spec:** `docs/superpowers/specs/2026-03-24-wiki-unification-design.md`

---

## 파일 맵

| 경로 | 액션 | 역할 |
|------|------|------|
| `NetworkModuleTest/Doc/Wiki/Home.md` | Create | Wiki 홈 (프로젝트 소개 + 페이지 목록) |
| `NetworkModuleTest/Doc/Wiki/_Sidebar.md` | Create | GitHub Wiki 사이드바 네비게이션 |
| `NetworkModuleTest/Doc/Wiki/01-Overall-Architecture.md` | Create | 전체 2계층 구조, 플랫폼 분기 |
| `NetworkModuleTest/Doc/Wiki/02-Network-Engine.md` | Create | AsyncIOProvider, 플랫폼 백엔드 |
| `NetworkModuleTest/Doc/Wiki/03-Session-Layer.md` | Create | Session, SessionManager, SessionPool |
| `NetworkModuleTest/Doc/Wiki/04-Concurrency.md` | Create | ExecutionQueue, KeyedDispatcher, AsyncScope, Channel, TimerQueue |
| `NetworkModuleTest/Doc/Wiki/05-Database.md` | Create | IDatabase, ConnectionPool, DatabaseFactory, 멀티DB |
| `NetworkModuleTest/Doc/Wiki/06-Buffer-Memory.md` | Create | IBufferPool, RIO/IOUring/Standard 구현 |
| `NetworkModuleTest/Doc/Wiki/07-Shutdown-Reconnect.md` | Create | Graceful Shutdown 순서, DB 재연결 백오프 |
| `NetworkModuleTest/Doc/Wiki/08-Build-and-Run.md` | Create | 빌드 명령, 실행 순서, 포트 정보 |
| `NetworkModuleTest/Doc/Wiki/diagrams/*.mmd` | Create | Mermaid 소스 파일 (페이지당 1개 이상) |
| `NetworkModuleTest/Doc/Wiki/assets/*.svg` | Generated | SVG 렌더링 결과 |
| `NetworkModuleTest/Doc/Wiki/scripts/generate-diagrams.ps1` | Create | `.mmd` → `.svg`/`.png` 렌더링 |
| `NetworkModuleTest/Doc/Wiki/scripts/publish-wiki.ps1` | Create | Doc/Wiki/ → wiki repo push |
| `.git/hooks/pre-push` | Create/Modify | Doc/Wiki/ 변경 시 publish-wiki.ps1 호출 |

**삭제 예정 (Task 10 이후):**
- `NetworkModuleTest/Doc/WikiDraft/ServerStructure/`
- `NetworkModuleTest/Doc/Reports/WikiPackage/`
- `NetworkModuleTest/Doc/Reports/_scripts/`

---

## Task 0: Feature 브랜치 생성

- [ ] **Step 1: 브랜치 생성**

```bash
cd E:/MyGitHub/PublicStudy
git checkout -b feature/wiki-unification
```

Expected: `Switched to a new branch 'feature/wiki-unification'`

---

## Task 1: Doc/Wiki 폴더 스캐폴딩

**Files:**
- Create: `NetworkModuleTest/Doc/Wiki/Home.md`
- Create: `NetworkModuleTest/Doc/Wiki/_Sidebar.md`
- Create: `NetworkModuleTest/Doc/Wiki/diagrams/.gitkeep`
- Create: `NetworkModuleTest/Doc/Wiki/assets/.gitkeep`

- [ ] **Step 1: 폴더 구조 생성**

```bash
mkdir -p E:/MyGitHub/PublicStudy/NetworkModuleTest/Doc/Wiki/diagrams
mkdir -p E:/MyGitHub/PublicStudy/NetworkModuleTest/Doc/Wiki/assets
mkdir -p E:/MyGitHub/PublicStudy/NetworkModuleTest/Doc/Wiki/scripts
```

- [ ] **Step 2: Home.md 작성**

`NetworkModuleTest/Doc/Wiki/Home.md`:
```markdown
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
```

- [ ] **Step 3: _Sidebar.md 작성**

`NetworkModuleTest/Doc/Wiki/_Sidebar.md`:
```markdown
**NetworkModuleTest**

- [홈](Home)
- [전체 구조](01-Overall-Architecture)
- [네트워크 엔진](02-Network-Engine)
- [세션 계층](03-Session-Layer)
- [동시성 런타임](04-Concurrency)
- [데이터베이스](05-Database)
- [버퍼/메모리](06-Buffer-Memory)
- [종료 및 재연결](07-Shutdown-Reconnect)
- [빌드 및 실행](08-Build-and-Run)
```

- [ ] **Step 4: .gitkeep 생성 및 커밋**

```bash
touch NetworkModuleTest/Doc/Wiki/diagrams/.gitkeep
touch NetworkModuleTest/Doc/Wiki/assets/.gitkeep
cd E:/MyGitHub/PublicStudy
git add NetworkModuleTest/Doc/Wiki/
git commit -m "chore: Doc/Wiki 폴더 스캐폴딩 — Home, Sidebar, 디렉터리 구조"
```

---

## Task 2: 01 전체 구조 페이지

**Files:**
- Read: `Doc/WikiDraft/ServerStructure/01-Overall-Architecture.md`
- Read: `Doc/Reports/WikiPackage/01-Overall-Architecture.md`
- Create: `Doc/Wiki/01-Overall-Architecture.md`
- Create: `Doc/Wiki/diagrams/01-overall-architecture.mmd`

**검증 기준 (작성 전 확인):**
- [ ] **Step 1: 기존 두 파일 읽기**

```bash
cat NetworkModuleTest/Doc/WikiDraft/ServerStructure/01-Overall-Architecture.md
cat NetworkModuleTest/Doc/Reports/WikiPackage/01-Overall-Architecture.md
```

- [ ] **Step 2: 01-Overall-Architecture.md 작성**

페이지 필수 포함 항목:
- 2계층 구조: `INetworkEngine`(상위) + `AsyncIOProvider`(플랫폼 백엔드)
- 플랫폼 선택 규칙: Windows(RIO→IOCP 폴백), Linux(io_uring→epoll 폴백), macOS(kqueue)
- 3개 컴포넌트: TestServer · TestDBServer(선택) · TestClient
- DB 비동기 흐름: 이벤트 핸들러 → `DBTaskQueue` → `IDatabase`
- Mermaid 다이어그램 인라인 + `![](assets/01-overall-architecture.svg)` fallback

- [ ] **Step 3: diagrams/01-overall-architecture.mmd 작성**

WikiDraft 01페이지의 Mermaid 소스 기반으로 작성. 테마 설정 포함:
```mermaid
%%{init: {'theme':'base', 'themeVariables': {
'fontFamily':'Segoe UI, Noto Sans KR, sans-serif',
'primaryColor':'#EAF4FF','primaryTextColor':'#0F172A',
'primaryBorderColor':'#3B82F6','lineColor':'#64748B'
}}}%%
flowchart LR
  ...
```

- [ ] **Step 4: 커밋**

```bash
git add NetworkModuleTest/Doc/Wiki/01-Overall-Architecture.md \
        NetworkModuleTest/Doc/Wiki/diagrams/01-overall-architecture.mmd
git commit -m "docs(wiki): 01 전체 구조 페이지 추가"
```

---

## Task 3: 02 네트워크 엔진 페이지

**Files:**
- Read: `Server/ServerEngine/Network/Core/AsyncIOProvider.h`
- Read: `Server/ServerEngine/Network/Core/BaseNetworkEngine.h`
- Read: `Server/ServerEngine/Network/Platforms/WindowsNetworkEngine.cpp` (accept loop 확인)
- Create: `Doc/Wiki/02-Network-Engine.md`
- Create: `Doc/Wiki/diagrams/02-network-engine.mmd`

**검증 기준:**
- [ ] **Step 1: 소스 코드 읽기 (코드 포인터 확보)**

```bash
grep -n "AcceptLoop\|ProcessCompletions\|AssociateSocket" \
  NetworkModuleTest/Server/ServerEngine/Network/Platforms/WindowsNetworkEngine.cpp | head -20
grep -n "AcceptLoop\|ProcessCompletions\|AssociateSocket" \
  NetworkModuleTest/Server/ServerEngine/Network/Platforms/LinuxNetworkEngine.cpp | head -20
```

- [ ] **Step 2: 02-Network-Engine.md 작성**

필수 포함 항목:
- `AsyncIOProvider` 역할: accept/recv/send 비동기 래퍼
- `BaseNetworkEngine`: 공통 로직 (ProcessRecvCompletion, ProcessCompletions)
- 플랫폼별 구현: Windows(RIO/IOCP), Linux(io_uring/epoll), macOS(kqueue)
- accept 흐름: `AcceptLoop()` → `SessionManager::CreateSession()` → `AssociateSocket()` → `OnConnected`
- 코드 포인터: Windows accept 루프 라인번호 포함

- [ ] **Step 3: diagrams/02-network-engine.mmd 작성**

accept + recv 완료 처리 흐름 다이어그램 (sequenceDiagram 또는 flowchart)

- [ ] **Step 4: 커밋**

```bash
git add NetworkModuleTest/Doc/Wiki/02-Network-Engine.md \
        NetworkModuleTest/Doc/Wiki/diagrams/02-network-engine.mmd
git commit -m "docs(wiki): 02 네트워크 엔진 페이지 추가"
```

---

## Task 4: 03 세션 계층 페이지

**Files:**
- Read: `Server/ServerEngine/Network/Core/Session.h`
- Read: `Server/ServerEngine/Network/Core/SessionManager.h`
- Read: `Server/ServerEngine/Network/Core/SessionPool.h`
- Read: `Doc/WikiDraft/ServerStructure/02-Session-Layer.md`
- Create: `Doc/Wiki/03-Session-Layer.md`
- Create: `Doc/Wiki/diagrams/03-session-layer.mmd`

- [ ] **Step 1: 소스 읽기**

```bash
grep -n "CreateSession\|SetSessionConfigurator\|Acquire\|Release" \
  NetworkModuleTest/Server/ServerEngine/Network/Core/SessionManager.h
grep -n "ProcessRawRecv\|SetOnRecv\|OnConnected\|OnDisconnected" \
  NetworkModuleTest/Server/ServerEngine/Network/Core/Session.h | head -20
```

- [ ] **Step 2: 03-Session-Layer.md 작성**

필수 포함 항목:
- `Session`: Initialize, SetOnRecv, OnConnected/OnDisconnected, ProcessRawRecv (TCP 재조립)
- `SessionPool`: Acquire/Release, 풀 기반 재사용
- `SessionManager`: CreateSession, SetSessionConfigurator 흐름
- 패킷 재조립: `PacketHeader(size, id)` 기반, 오버플로우 탐지
- classDiagram Mermaid

- [ ] **Step 3: diagrams/03-session-layer.mmd 작성**

classDiagram으로 Session · SessionPool · SessionManager · TestServer 관계 표현

- [ ] **Step 4: 커밋**

```bash
git add NetworkModuleTest/Doc/Wiki/03-Session-Layer.md \
        NetworkModuleTest/Doc/Wiki/diagrams/03-session-layer.mmd
git commit -m "docs(wiki): 03 세션 계층 페이지 추가"
```

---

## Task 5: 04 동시성 런타임 페이지 (신규)

**Files:**
- Read: `Server/ServerEngine/Concurrency/ExecutionQueue.h`
- Read: `Server/ServerEngine/Concurrency/KeyedDispatcher.h`
- Read: `Server/ServerEngine/Concurrency/AsyncScope.h`
- Read: `Server/ServerEngine/Concurrency/Channel.h`
- Read: `Server/ServerEngine/Concurrency/TimerQueue.h`
- Create: `Doc/Wiki/04-Concurrency.md`
- Create: `Doc/Wiki/diagrams/04-concurrency.mmd`

- [ ] **Step 1: 소스 읽기**

```bash
head -80 NetworkModuleTest/Server/ServerEngine/Concurrency/ExecutionQueue.h
head -60 NetworkModuleTest/Server/ServerEngine/Concurrency/KeyedDispatcher.h
head -50 NetworkModuleTest/Server/ServerEngine/Concurrency/AsyncScope.h
```

- [ ] **Step 2: 04-Concurrency.md 작성**

필수 포함 항목:
- `ExecutionQueue<T>`: BackpressurePolicy(RejectNewest/Block), mutex/lockfree 백엔드, 용량 제한
- `KeyedDispatcher`: 키 친화도(세션 ID 기반) 라우팅, 순서 보장
- `AsyncScope`: 인플라이트 작업 추적, Cancel/WaitForDrain
- `Channel<T>`: ExecutionQueue 래퍼, Send/Receive 의미론
- `TimerQueue`: 지연 실행, 반복 타이머
- DBTaskQueue/OrderedTaskQueue가 이 컴포넌트들 위에 어떻게 동작하는지 흐름 설명

- [ ] **Step 3: diagrams/04-concurrency.mmd 작성**

KeyedDispatcher → ExecutionQueue(worker별) 라우팅 흐름 다이어그램

- [ ] **Step 4: 커밋**

```bash
git add NetworkModuleTest/Doc/Wiki/04-Concurrency.md \
        NetworkModuleTest/Doc/Wiki/diagrams/04-concurrency.mmd
git commit -m "docs(wiki): 04 동시성 런타임 페이지 추가 (신규)"
```

---

## Task 6: 05 데이터베이스 페이지 (신규)

**Files:**
- Read: `Server/ServerEngine/Interfaces/IDatabase.h`
- Read: `Server/ServerEngine/Database/DatabaseFactory.h`
- Read: `Server/ServerEngine/Database/ConnectionPool.h`
- Create: `Doc/Wiki/05-Database.md`
- Create: `Doc/Wiki/diagrams/05-database.mmd`

- [ ] **Step 1: 소스 읽기**

```bash
cat NetworkModuleTest/Server/ServerEngine/Interfaces/IDatabase.h
head -60 NetworkModuleTest/Server/ServerEngine/Database/ConnectionPool.h
head -40 NetworkModuleTest/Server/ServerEngine/Database/DatabaseFactory.h
```

- [ ] **Step 2: 05-Database.md 작성**

필수 포함 항목:
- `IDatabase` 인터페이스: Connect/Disconnect/Execute/Query
- `DatabaseFactory`: DatabaseType enum → 구체 구현 생성, 플랫폼 조건 (ODBC/OLEDB=Windows전용, PostgreSQL=HAVE_LIBPQ, SQLite=HAVE_SQLITE3)
- `ConnectionPool`: 풀 크기, 대여/반납
- 지원 백엔드: SQLite · ODBC · OLEDB · PostgreSQL · Mock
- TestServer에서 비동기 DB 처리 흐름: 이벤트 핸들러 → `DBTaskQueue` enqueue → 워커 dequeue → `IDatabase::Execute`

- [ ] **Step 3: diagrams/05-database.mmd 작성**

IDatabase 계층 + DBTaskQueue → ConnectionPool → IDatabase 흐름

- [ ] **Step 4: 커밋**

```bash
git add NetworkModuleTest/Doc/Wiki/05-Database.md \
        NetworkModuleTest/Doc/Wiki/diagrams/05-database.mmd
git commit -m "docs(wiki): 05 데이터베이스 페이지 추가 (신규)"
```

---

## Task 7: 06 버퍼/메모리 페이지 (신규)

**Files:**
- Read: `Server/ServerEngine/Core/Memory/IBufferPool.h`
- Read: `Server/ServerEngine/Core/Memory/RIOBufferPool.h`
- Read: `Server/ServerEngine/Core/Memory/IOUringBufferPool.h`
- Read: `Server/ServerEngine/Core/Memory/StandardBufferPool.h`
- Create: `Doc/Wiki/06-Buffer-Memory.md`
- Create: `Doc/Wiki/diagrams/06-buffer-memory.mmd`

- [ ] **Step 1: 소스 읽기**

```bash
cat NetworkModuleTest/Server/ServerEngine/Core/Memory/IBufferPool.h
head -40 NetworkModuleTest/Server/ServerEngine/Core/Memory/RIOBufferPool.h
head -40 NetworkModuleTest/Server/ServerEngine/Core/Memory/IOUringBufferPool.h
head -40 NetworkModuleTest/Server/ServerEngine/Core/Memory/StandardBufferPool.h
```

- [ ] **Step 2: 06-Buffer-Memory.md 작성**

필수 포함 항목:
- `IBufferPool`: Allocate/Deallocate 인터페이스
- `StandardBufferPool`: posix_memalign / `_aligned_malloc`, 등록 없음
- `RIOBufferPool`: VirtualAlloc + `RIORegisterBuffer` 1회 등록 (Windows 8+), per-op pin 비용 없음
- `IOUringBufferPool`: `posix_memalign` + `io_uring_register_buffers` (Linux), zero-copy 고정 버퍼
- `AsyncBufferPool` using alias: 플랫폼에 따라 RIO 또는 IOUring으로 자동 선택
- `SendBufferPool` (Network 레이어): 송신 버퍼 관리

- [ ] **Step 3: diagrams/06-buffer-memory.mmd 작성**

IBufferPool 계층 + 플랫폼 분기 다이어그램

- [ ] **Step 4: 커밋**

```bash
git add NetworkModuleTest/Doc/Wiki/06-Buffer-Memory.md \
        NetworkModuleTest/Doc/Wiki/diagrams/06-buffer-memory.mmd
git commit -m "docs(wiki): 06 버퍼/메모리 페이지 추가 (신규)"
```

---

## Task 8: 07·08 기존 페이지 재작성

**Files:**
- Read: `Doc/WikiDraft/ServerStructure/04-Graceful-Shutdown.md`
- Read: `Doc/WikiDraft/ServerStructure/05-Reconnect-Strategy.md`
- Read: `Doc/Reports/WikiPackage/04-Graceful-Shutdown-and-Reconnect.md`
- Read: `Doc/Reports/WikiPackage/05-Operational-Notes.md`
- Create: `Doc/Wiki/07-Shutdown-Reconnect.md`
- Create: `Doc/Wiki/diagrams/07-shutdown.mmd`
- Create: `Doc/Wiki/08-Build-and-Run.md`

- [ ] **Step 1: 기존 파일 읽기**

```bash
cat NetworkModuleTest/Doc/WikiDraft/ServerStructure/04-Graceful-Shutdown.md
cat NetworkModuleTest/Doc/WikiDraft/ServerStructure/05-Reconnect-Strategy.md
cat NetworkModuleTest/Doc/Reports/WikiPackage/04-Graceful-Shutdown-and-Reconnect.md
cat NetworkModuleTest/Doc/Reports/WikiPackage/05-Operational-Notes.md
```

- [ ] **Step 2: 07-Shutdown-Reconnect.md 작성**

필수 포함 항목 (두 파일 내용 통합):
- `TestServer::Stop()` 종료 순서: DBTaskQueue 드레인 → DB 해제 → 네트워크 종료
- DB 재연결 루프: 지수 백오프 (1s→2s→4s…max 30s), `WSAECONNREFUSED`=1초 고정
- `condition_variable`로 `Stop()` 시 즉시 깨어나는 설계
- 코드 포인터: TestServer.cpp의 재연결 루프·에러 구분 라인
- Mermaid flowchart (shutdown 순서)

- [ ] **Step 3: 08-Build-and-Run.md 작성**

필수 포함 항목:
- MSBuild 명령 (Debug/x64)
- 실행 순서: TestDBServer(8002) → TestServer(9000) → TestClient
- 주요 run 스크립트: `run_allServer.ps1`, `run_client.ps1`
- Linux/Docker: `test_linux/` 폴더, `run_docker_test.ps1`

- [ ] **Step 4: 커밋**

```bash
git add NetworkModuleTest/Doc/Wiki/07-Shutdown-Reconnect.md \
        NetworkModuleTest/Doc/Wiki/diagrams/07-shutdown.mmd \
        NetworkModuleTest/Doc/Wiki/08-Build-and-Run.md
git commit -m "docs(wiki): 07 종료·재연결, 08 빌드·실행 페이지 추가"
```

---

## Task 9: generate-diagrams.ps1 + SVG 렌더링

**Files:**
- Create: `Doc/Wiki/scripts/generate-diagrams.ps1`

- [ ] **Step 1: generate-diagrams.ps1 작성**

WikiDraft의 `scripts/generate-diagrams.ps1`을 참고해 작성:
```bash
cat NetworkModuleTest/Doc/WikiDraft/ServerStructure/scripts/generate-diagrams.ps1
```

수정 사항:
- 입력 경로: `$PSScriptRoot/../diagrams/*.mmd`
- 출력 경로: `$PSScriptRoot/../assets/`
- mmdc(mermaid-cli) 사용, 없으면 npx mmdc 폴백

- [ ] **Step 2: 스크립트 실행 (SVG 생성)**

```powershell
cd E:\MyGitHub\PublicStudy\NetworkModuleTest
.\Doc\Wiki\scripts\generate-diagrams.ps1
```

Expected: `Doc/Wiki/assets/` 하위에 각 `.mmd`에 대응하는 `.svg` 파일 생성

- [ ] **Step 3: 생성된 SVG 확인**

```bash
ls NetworkModuleTest/Doc/Wiki/assets/
```

Expected: `01-overall-architecture.svg`, `02-network-engine.svg` 등 7개 이상

- [ ] **Step 4: assets 커밋**

```bash
git add NetworkModuleTest/Doc/Wiki/scripts/generate-diagrams.ps1 \
        NetworkModuleTest/Doc/Wiki/assets/
git commit -m "docs(wiki): Mermaid 다이어그램 SVG 렌더링 및 generate-diagrams.ps1 추가"
```

---

## Task 10: publish-wiki.ps1 + pre-push hook

**Files:**
- Create: `Doc/Wiki/scripts/publish-wiki.ps1`
- Create/Modify: `.git/hooks/pre-push`

- [ ] **Step 1: publish-wiki.ps1 작성**

`NetworkModuleTest/Doc/Wiki/scripts/publish-wiki.ps1`:
```powershell
# publish-wiki.ps1 — Doc/Wiki/ 내용을 PublicStudy.wiki.git에 동기화
param(
  [string]$WikiUrl = ""
)

$RepoRoot = Resolve-Path "$PSScriptRoot/../../../.."
$WikiSource = "$RepoRoot/NetworkModuleTest/Doc/Wiki"
$TempDir = Join-Path $env:TEMP "wiki-publish-$(Get-Random)"

# URL 결정: SSH 우선, 없으면 GH_TOKEN 폴백
if ($WikiUrl -eq "") {
  $SshTest = & git ls-remote git@github.com:leedg/PublicStudy.wiki.git HEAD 2>&1
  if ($LASTEXITCODE -eq 0) {
    $WikiUrl = "git@github.com:leedg/PublicStudy.wiki.git"
  } elseif ($env:GH_TOKEN) {
    $WikiUrl = "https://$($env:GH_TOKEN)@github.com/leedg/PublicStudy.wiki.git"
  } else {
    Write-Error "SSH key or GH_TOKEN required"
    exit 1
  }
}

try {
  Write-Host "Cloning wiki repo..."
  git clone $WikiUrl $TempDir

  # 기존 파일 삭제 후 전체 복사 (assets/, diagrams/ 서브디렉터리 포함)
  Get-ChildItem $TempDir -Exclude ".git" | Remove-Item -Recurse -Force
  Copy-Item "$WikiSource/*" $TempDir -Recurse -Force

  Set-Location $TempDir
  git add -A
  $HasChanges = git status --porcelain
  if ($HasChanges) {
    git commit -m "docs: sync wiki from main $(Get-Date -Format 'yyyy-MM-dd')"
    git push
    Write-Host "Wiki published successfully."
  } else {
    Write-Host "No changes to publish."
  }
} finally {
  Set-Location $RepoRoot
  if (Test-Path $TempDir) { Remove-Item $TempDir -Recurse -Force }
}
```

- [ ] **Step 2: publish-wiki.ps1 수동 실행 테스트**

```powershell
cd E:\MyGitHub\PublicStudy\NetworkModuleTest
.\Doc\Wiki\scripts\publish-wiki.ps1
```

Expected: GitHub Wiki에 페이지 8개가 올라가고 에러 없이 종료

- [ ] **Step 3: GitHub Wiki 확인**

브라우저에서 `https://github.com/leedg/PublicStudy/wiki` 접속하여 8페이지가 정상 표시되는지 확인:
- Home, 01~08 페이지 존재
- _Sidebar 네비게이션 정상 작동
- Mermaid 다이어그램 렌더링 확인

- [ ] **Step 4: pre-push hook 작성 — 커밋 가능한 소스 파일로 저장**

hook은 `.git/hooks/`에 복사 설치하는 방식으로 관리한다.
git clone 후 `.git/hooks/`는 초기화되므로, hook 소스를 repo에 커밋해두고 설치 스크립트로 복사한다.

`NetworkModuleTest/Doc/Wiki/scripts/pre-push.hook` (bash 스크립트):
```bash
#!/usr/bin/env bash
# pre-push hook: main 브랜치 push 시 Doc/Wiki/ 변경이 있으면 publish-wiki.ps1 실행
# 설치: cp pre-push.hook ../../../../.git/hooks/pre-push && chmod +x ../../../../.git/hooks/pre-push

# main 브랜치 push 시에만 동작 (feature/test 브랜치에서는 실행하지 않음)
CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
if [ "$CURRENT_BRANCH" != "main" ]; then
  exit 0
fi

REPO_ROOT=$(git rev-parse --show-toplevel)
SCRIPT="$REPO_ROOT/NetworkModuleTest/Doc/Wiki/scripts/publish-wiki.ps1"

changed=""
while read local_ref local_sha remote_ref remote_sha; do
  if [ "$remote_sha" = "0000000000000000000000000000000000000000" ]; then
    base=$(git rev-list --max-parents=0 "$local_sha")
  else
    base="$remote_sha"
  fi
  changed=$(git diff --name-only "$base" "$local_sha" | grep "^NetworkModuleTest/Doc/Wiki/")
  [ -n "$changed" ] && break
done

if [ -n "$changed" ]; then
  echo "Doc/Wiki/ changes detected — publishing to GitHub Wiki..."
  powershell.exe -File "$SCRIPT"
fi
```

- [ ] **Step 4b: .git/hooks/에 설치**

```bash
cp NetworkModuleTest/Doc/Wiki/scripts/pre-push.hook .git/hooks/pre-push
chmod +x .git/hooks/pre-push
```

**Note:** `.git/hooks/pre-push`는 git으로 추적되지 않는다. 새 clone 후에는 위 cp 명령을 다시 실행해야 한다.

- [ ] **Step 5: hook 동작 확인**

별도 테스트 브랜치에서 확인 (feature 브랜치의 커밋 히스토리 오염 방지):
```bash
git checkout -b test/wiki-hook-verify
echo "" >> NetworkModuleTest/Doc/Wiki/Home.md
git add NetworkModuleTest/Doc/Wiki/Home.md
git commit -m "test: pre-push hook 동작 확인 (삭제 예정)"
git push origin test/wiki-hook-verify
```

Expected: push 시 "Doc/Wiki/ changes detected — publishing to GitHub Wiki..." 출력 후 wiki 업데이트

- [ ] **Step 6: 테스트 브랜치 삭제**

```bash
git checkout feature/wiki-unification
git branch -d test/wiki-hook-verify
git push origin --delete test/wiki-hook-verify
```

- [ ] **Step 7: publish-wiki.ps1 + hook 소스 커밋**

```bash
git add NetworkModuleTest/Doc/Wiki/scripts/publish-wiki.ps1 \
        NetworkModuleTest/Doc/Wiki/scripts/pre-push.hook
git commit -m "feat(wiki): publish-wiki.ps1 및 pre-push hook 소스 추가"
```

---

## Task 11: 구 폴더 삭제 및 정리

> **전제 조건:** Task 10 Step 3에서 GitHub Wiki 정상 발행 확인 완료 후 진행

**Files:**
- Delete: `NetworkModuleTest/Doc/WikiDraft/`
- Delete: `NetworkModuleTest/Doc/Reports/WikiPackage/`
- Delete: `NetworkModuleTest/Doc/Reports/_scripts/`

- [ ] **Step 1: WikiPackage_Reference.docx 내용 확인 및 아카이브 결정**

```bash
ls NetworkModuleTest/Doc/Reports/WikiPackage/
```

`WikiPackage_Reference.docx`를 열어 내용 확인:
- **내용이 Wiki 페이지에 이미 반영됨** → 그대로 `git rm` 진행
- **미반영 내용 있음** → 해당 내용을 관련 Wiki 페이지에 추가 후 커밋
- **별도 보관 필요** → `git mv NetworkModuleTest/Doc/Reports/WikiPackage/WikiPackage_Reference.docx NetworkModuleTest/Doc/Reports/WikiPackage_Reference.docx` 실행 후 삭제 진행

- [ ] **Step 2: 구 폴더 삭제**

```bash
git rm -r NetworkModuleTest/Doc/WikiDraft/
git rm -r NetworkModuleTest/Doc/Reports/WikiPackage/
git rm -r NetworkModuleTest/Doc/Reports/_scripts/
```

- [ ] **Step 3: 커밋**

```bash
git commit -m "chore: WikiDraft, WikiPackage, _scripts 삭제 — Doc/Wiki로 통합 완료"
```

- [ ] **Step 4: push 및 최종 확인**

```bash
git push
```

Expected: pre-push hook 실행, GitHub Wiki 최신 상태 유지 확인
