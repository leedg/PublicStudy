# Code Cleanup Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Remove unused code, fix duplicate/inconsistent patterns, and validate the result with a build + smoke test.

**Architecture:** Pure cleanup — no functional changes, no new abstractions. Every edit must preserve observable behavior. After all tasks, run MSBuild (0 errors 0 warnings target) and a process smoke-test (DBServer + TestServer start/stop clean).

**Tech Stack:** C++17, MSBuild/VS2022, WinSock2, SQLite

---

## Scope (what changes, what stays)

| Item | Decision | Reason |
|------|----------|--------|
| `SaveGameProgress`, `Custom` enum values | **Remove** | No implementation anywhere; `default` case only logs an error |
| `<iostream>` in TestServer.cpp | **Remove** | Not used (Logger replaces it) |
| `<cstring>` in ClientSession.cpp | **Remove** | Not used |
| `lock_guard` after `thread.join()` in DBTaskQueue::Shutdown() | **Remove** | Threads already joined; mutex serves no purpose |
| Double-language `// English:` / `// 한글:` comment pairs | **Keep English, remove Korean** | Redundant information; standard code comment language |
| StringUtil.h vs StringUtils.h | **Keep both** | Different targets: `char[]` vs `std::string` |
| `mPort(0)` / `mIsRunning(false)` in constructor | **Keep** | Valid safety initialization; `Initialize()` assignment is intentional |
| Namespace style | **Keep as-is** | Already consistently `A::B` scoped form |
| VS2022/VS2025 compatibility | **No change needed** | C++17 `std::optional`, `localtime_s`, `inet_pton` all supported |

---

## Build command (reference)

```powershell
powershell.exe -Command "& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe' 'E:\MyGitHub\PublicStudy\NetworkModuleTest\NetworkModuleTest.sln' /p:Configuration=Debug /p:Platform=x64 /m /nologo 2>&1 | Select-Object -Last 10"
```

Target: **0 errors, 0 warnings**

---

## Task 1 — Remove UNIMPLEMENTED enum values

**Files:**
- Modify: `Server/TestServer/include/DBTaskQueue.h`
- Modify: `Server/TestServer/src/DBTaskQueue.cpp`

**Step 1: Edit DBTaskQueue.h**

Remove the two unimplemented values and their comments from `enum class DBTaskType`:
```cpp
// BEFORE (lines ~32-40)
enum class DBTaskType : uint8_t
{
    RecordConnectTime,
    RecordDisconnectTime,
    UpdatePlayerData,
    SaveGameProgress,    // ← REMOVE (+ comment)
    Custom               // ← REMOVE (+ comment)
};

// AFTER
enum class DBTaskType : uint8_t
{
    RecordConnectTime,
    RecordDisconnectTime,
    UpdatePlayerData,
};
```

**Step 2: Edit DBTaskQueue.cpp — ProcessTask() default case**

The `default:` branch currently prints "SaveGameProgress and Custom are not yet implemented".
Replace with a generic unknown-type message:
```cpp
// BEFORE
default:
    result = "Unknown task type (SaveGameProgress and Custom are not yet implemented)";
    Logger::Error("Unknown DB task type - SaveGameProgress and Custom tasks are not yet implemented");
    break;

// AFTER
default:
    result = "Unknown DB task type: " + std::to_string(static_cast<int>(task.type));
    Logger::Error("Unknown DB task type: " + std::to_string(static_cast<int>(task.type)));
    break;
```

**Step 3: Build**
```powershell
powershell.exe -Command "& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe' 'E:\MyGitHub\PublicStudy\NetworkModuleTest\NetworkModuleTest.sln' /p:Configuration=Debug /p:Platform=x64 /m /nologo 2>&1 | Select-Object -Last 10"
```
Expected: `0 Error(s)` — the enum values were only used in the `default` branch, so removing them causes no compile errors.

**Step 4: Commit**
```bash
git add Server/TestServer/include/DBTaskQueue.h Server/TestServer/src/DBTaskQueue.cpp
git commit -m "refactor: remove unimplemented SaveGameProgress and Custom DB task types"
```

---

## Task 2 — Remove unused includes

**Files:**
- Modify: `Server/TestServer/src/TestServer.cpp`  (remove `<iostream>`)
- Modify: `Server/TestServer/src/ClientSession.cpp`  (remove `<cstring>`)

**Step 1: TestServer.cpp**

Find and delete the `#include <iostream>` line.
Logger (via NetworkUtils.h) handles all output; `std::cout` is not called anywhere in this file.

**Step 2: ClientSession.cpp**

Find and delete the `#include <cstring>` line.
No `memcpy`, `strcmp`, `strlen`, or similar C-string functions are called directly.

**Step 3: Build**
```powershell
powershell.exe -Command "& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe' 'E:\MyGitHub\PublicStudy\NetworkModuleTest\NetworkModuleTest.sln' /p:Configuration=Debug /p:Platform=x64 /m /nologo 2>&1 | Select-Object -Last 10"
```
Expected: `0 Error(s)`

**Step 4: Commit**
```bash
git add Server/TestServer/src/TestServer.cpp Server/TestServer/src/ClientSession.cpp
git commit -m "refactor: remove unused <iostream> and <cstring> includes"
```

---

## Task 3 — Remove unnecessary mutex lock after thread join

**Files:**
- Modify: `Server/TestServer/src/DBTaskQueue.cpp` — `Shutdown()` function

**Context:**
After all worker threads are `join()`ed (lines ~158-163), no other thread can access `worker->taskQueue`. The `lock_guard` on line ~174 is therefore dead synchronization.

**Step 1: Edit Shutdown() drain loop**

```cpp
// BEFORE (~lines 170-189)
for (auto& worker : mWorkers)
{
    std::vector<DBTask> drainTasks;
    {
        // Threads are already joined so no lock contention, but we lock for consistency.
        std::lock_guard<std::mutex> lock(worker->mutex);   // ← REMOVE this scope + lock
        while (!worker->taskQueue.empty())
        {
            drainTasks.push_back(std::move(worker->taskQueue.front()));
            worker->taskQueue.pop();
        }
    }                                                       // ← REMOVE closing brace
    // execute drain tasks...
}

// AFTER
for (auto& worker : mWorkers)
{
    std::vector<DBTask> drainTasks;
    while (!worker->taskQueue.empty())
    {
        drainTasks.push_back(std::move(worker->taskQueue.front()));
        worker->taskQueue.pop();
    }
    // execute drain tasks...
}
```

Also update the comment above the loop from "but we lock for consistency" to just "All workers joined; drain without lock."

**Step 2: Build**
```powershell
powershell.exe -Command "& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe' 'E:\MyGitHub\PublicStudy\NetworkModuleTest\NetworkModuleTest.sln' /p:Configuration=Debug /p:Platform=x64 /m /nologo 2>&1 | Select-Object -Last 10"
```
Expected: `0 Error(s)`

**Step 3: Commit**
```bash
git add Server/TestServer/src/DBTaskQueue.cpp
git commit -m "refactor: remove unnecessary mutex lock in Shutdown() drain loop after thread join"
```

---

## Task 4 — Comment cleanup: remove Korean parallel lines

**Files (all affected):**
- `Server/ServerEngine/Utils/StringUtil.h`
- `Server/ServerEngine/Utils/StringUtils.h`
- `Server/TestServer/include/DBTaskQueue.h`
- `Server/TestServer/src/DBTaskQueue.cpp`
- `Server/TestServer/src/TestServer.cpp`
- `Server/TestServer/src/ClientSession.cpp`
- Additional files that have the `// 한글:` pattern (scan with grep)

**Rule:**
- Keep `// English: ...` lines as-is, renaming them to just `//` (drop the "English:" prefix)
- Remove every `// 한글: ...` line and its associated blank/indent lines
- File-header block comments (top 5-10 lines explaining the file) — these can keep both languages OR just keep English. Apply the same rule for consistency.

**Step 1: Grep for scope**
```bash
grep -rn "// 한글:" Server/ --include="*.h" --include="*.cpp" | wc -l
```
Record the count. Target: 0 after cleanup.

**Step 2: Edit each file**

For each file with `// 한글:` lines:
1. Delete every line that starts with `// 한글:` (including continuation lines that are indented Korean text without the `// 한글:` prefix but are clearly the Korean continuation).
2. For the corresponding `// English:` line, remove the `English:` prefix → just `//`.

Example transform:
```cpp
// BEFORE
// English: Initialize the task queue with N worker threads and a WAL file.
// 한글: N개 워커 스레드와 WAL 파일로 태스크 큐를 초기화한다.
bool Initialize(...);

// AFTER
// Initialize the task queue with N worker threads and a WAL file.
bool Initialize(...);
```

**Step 3: Verify no Korean characters remain in comment lines**
```bash
grep -rn "한글\| Korean\|// 한\|// 구\|// 비\|// 모\|// 세\|// 워\|// 스\|// 서\|// 클" Server/ --include="*.h" --include="*.cpp"
```
(Broad grep to catch stray Korean. Review manually.)

**Step 4: Build**
```powershell
powershell.exe -Command "& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe' 'E:\MyGitHub\PublicStudy\NetworkModuleTest\NetworkModuleTest.sln' /p:Configuration=Debug /p:Platform=x64 /m /nologo 2>&1 | Select-Object -Last 10"
```
Expected: `0 Error(s), 0 Warning(s)`

**Step 5: Commit**
```bash
git add Server/
git commit -m "refactor: remove duplicate Korean comment lines, keep English only"
```

---

## Task 5 — Build test (full clean rebuild)

**Step 1: Clean build**
```powershell
powershell.exe -Command "& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe' 'E:\MyGitHub\PublicStudy\NetworkModuleTest\NetworkModuleTest.sln' /t:Clean,Build /p:Configuration=Debug /p:Platform=x64 /m /nologo 2>&1 | Select-Object -Last 15"
```

Expected output must contain:
```
Build succeeded.
    0 Error(s)
    0 Warning(s)
```

**Step 2: If any warnings appear**, address them before proceeding. Do NOT suppress with pragmas.

---

## Task 6 — Smoke test (process start/stop)

**Purpose:** Verify that executables start, bind ports, and stop gracefully after cleanup.

**Step 1: Start TestDBServer (background)**
```powershell
Start-Process -FilePath "E:\MyGitHub\PublicStudy\NetworkModuleTest\x64\Debug\TestDBServer.exe" -ArgumentList "-p 8002" -PassThru | Select-Object Id
```
Wait 2 seconds, then verify port 8002 is listening:
```powershell
netstat -an | findstr :8002
```
Expected: `TCP    0.0.0.0:8002    ... LISTENING`

**Step 2: Start TestServer (background)**
```powershell
Start-Process -FilePath "E:\MyGitHub\PublicStudy\NetworkModuleTest\x64\Debug\TestServer.exe" -ArgumentList "-p 9000 --db" -PassThru | Select-Object Id
```
Wait 2 seconds, verify port 9000:
```powershell
netstat -an | findstr :9000
```
Expected: `TCP    0.0.0.0:9000    ... LISTENING`

**Step 3: Signal graceful shutdown (named event)**
```powershell
# Trigger graceful shutdown via named event
$event = [System.Threading.EventWaitHandle]::OpenExisting("TestServer_GracefulShutdown")
$event.Set()
Start-Sleep -Seconds 3
netstat -an | findstr :9000
```
Expected: port 9000 no longer listed (server stopped).

Stop DBServer:
```powershell
Stop-Process -Name "TestDBServer" -Force
```

**Step 4: Check no crash dump files created**
```powershell
Get-ChildItem -Path "E:\MyGitHub\PublicStudy\NetworkModuleTest\x64\Debug\dumps\" -Filter "*.dmp" -ErrorAction SilentlyContinue
```
Expected: empty (no .dmp files)

---

## Task 7 — Update documentation

**Files:**
- `Doc/Reports/_scripts/Build-NetworkAsyncDB-Report.py` — update version to 1.3, date to 2026-03-15, add a Cleanup section in Changelog
- Regenerate `Doc/Reports/Network_Async_DB_Report.docx`

**Step 1: Update report script**

In `Build-NetworkAsyncDB-Report.py`:
- Change version: `"1.2"` → `"1.3"`
- Change date: `"2026-03-10"` → `"2026-03-15"` (or current date)
- In `section_changelog()`, add a new row:

```python
("2026-03-15", "v1.3",
 "코드 정리: 미구현 DBTaskType enum 제거, 미사용 include 제거, "
 "Shutdown() 불필요 mutex 제거, 이중 언어 주석 영어 단일화"),
```

**Step 2: Regenerate docx**
```powershell
cd "E:\MyGitHub\PublicStudy\NetworkModuleTest\Doc\Reports\_scripts"
python Build-NetworkAsyncDB-Report.py
```
Expected: `Network_Async_DB_Report.docx` updated (file timestamp changes).

**Step 3: Commit everything**
```bash
git add Doc/Reports/_scripts/Build-NetworkAsyncDB-Report.py Doc/Reports/Network_Async_DB_Report.docx
git commit -m "docs: v1.3 — code cleanup 반영 (unused code 제거, 주석 정리)"
```

---

## Task 8 — Final push

```bash
git log --oneline -6
git push origin main
```

Verify: all cleanup commits appear in the remote `main` branch.
