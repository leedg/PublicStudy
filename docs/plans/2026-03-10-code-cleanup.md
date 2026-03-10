# Code Cleanup Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Fix UB in ExecutionQueue CV usage, add missing fcntl guard in LinuxNetworkEngine, clean up formatting, and add .gitignore for runtime artifacts.

**Architecture:** Four independent, targeted edits — no cross-file dependencies. Each commit is self-contained and reversible. CV split follows the existing mNotFullMutexCV / mNotFullLFCV naming pattern already in the codebase.

**Tech Stack:** C++17, std::condition_variable, POSIX fcntl

---

### Task 1: fix(critical) — ExecutionQueue mNotEmptyCV UB

**Problem:** `mNotEmptyCV` is waited with `mMutexQueueMutex` (Mutex backend) AND `mWaitMutex` (LockFree backend). C++ standard requires one CV ↔ one mutex. This is UB.

**Files:**
- Modify: `NetworkModuleTest/Server/ServerEngine/Concurrency/ExecutionQueue.h`

**Step 1: Rename member declarations (bottom of file, ~line 884)**

Find:
```cpp
std::condition_variable mNotEmptyCV;
```

Replace with:
```cpp
std::condition_variable mNotEmptyMutexCV;  // Mutex backend only (with mMutexQueueMutex)
std::condition_variable mNotEmptyLFCV_pop; // LockFree backend Pop (with mWaitMutex)
```

**Step 2: Update TryPushMutex (~line 358)**

Find:
```cpp
mNotEmptyCV.notify_one();
```
(The one immediately after the closing `}` of the `lock_guard` scope, inside `TryPushMutex`)

Replace with:
```cpp
mNotEmptyMutexCV.notify_one();
```

**Step 3: Update TryPushLockFree (~line 394)**

Find:
```cpp
		std::lock_guard<std::mutex> wl(mWaitMutex);

		mNotEmptyCV.notify_one();
```

Replace with:
```cpp
		std::lock_guard<std::mutex> wl(mWaitMutex);

		mNotEmptyLFCV_pop.notify_one();
```

**Step 4: Update PopMutexWait (~line 678 and ~line 692)**

Find (two occurrences inside PopMutexWait):
```cpp
			mNotEmptyCV.wait(lock, [this] {
```
```cpp
			if (!mNotEmptyCV.wait_until(lock, deadline, [this] {
```

Replace respectively with:
```cpp
			mNotEmptyMutexCV.wait(lock, [this] {
```
```cpp
			if (!mNotEmptyMutexCV.wait_until(lock, deadline, [this] {
```

**Step 5: Update PopLockFreeWait (~line 788 and ~line 802)**

Find (two occurrences inside PopLockFreeWait):
```cpp
			mNotEmptyCV.wait(lock, [this] {
```
```cpp
			if (!mNotEmptyCV.wait_until(lock, deadline, [this] {
```

Replace respectively with:
```cpp
			mNotEmptyLFCV_pop.wait(lock, [this] {
```
```cpp
			if (!mNotEmptyLFCV_pop.wait_until(lock, deadline, [this] {
```

**Step 6: Update Shutdown (~line 278)**

Find:
```cpp
	mNotEmptyCV.notify_all();
```

Replace with:
```cpp
	mNotEmptyMutexCV.notify_all();

	mNotEmptyLFCV_pop.notify_all();
```

**Step 7: Verify — grep must return zero results**

```bash
grep -n "mNotEmptyCV" NetworkModuleTest/Server/ServerEngine/Concurrency/ExecutionQueue.h
```
Expected: no output (all renamed).

**Step 8: Commit**

```bash
git add NetworkModuleTest/Server/ServerEngine/Concurrency/ExecutionQueue.h
git commit -m "fix(critical): ExecutionQueue — split mNotEmptyCV per backend to eliminate UB

mNotEmptyCV was waited under mMutexQueueMutex (Mutex backend) and
mWaitMutex (LockFree backend) — using one CV with two different mutexes
is undefined behaviour per [thread.condition.condvar].

Split into:
  mNotEmptyMutexCV — Mutex backend (mMutexQueueMutex)
  mNotEmptyLFCV_pop — LockFree backend (mWaitMutex)

Follows existing mNotFullMutexCV / mNotFullLFCV naming pattern.
No behaviour change; correctness fix only.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 2: fix — LinuxNetworkEngine fcntl F_GETFL guard

**Problem:** `fcntl(F_GETFL)` can return -1 on error; passing `-1 | O_NONBLOCK` to `F_SETFL` corrupts flags. macOS already has this guard; Linux is missing it.

**Files:**
- Modify: `NetworkModuleTest/Server/ServerEngine/Network/Platforms/LinuxNetworkEngine.cpp`

**Step 1: Add guard (~line 221)**

Find:
```cpp
		int flags = fcntl(clientSocket, F_GETFL, 0);
		fcntl(clientSocket, F_SETFL, flags | O_NONBLOCK);
```

Replace with:
```cpp
		int flags = fcntl(clientSocket, F_GETFL, 0);
		if (flags != -1)
		{
			fcntl(clientSocket, F_SETFL, flags | O_NONBLOCK);
		}
```

**Step 2: Verify symmetry with macOS**

```bash
grep -A3 "F_GETFL" NetworkModuleTest/Server/ServerEngine/Network/Platforms/LinuxNetworkEngine.cpp
grep -A3 "F_GETFL" NetworkModuleTest/Server/ServerEngine/Network/Platforms/macOSNetworkEngine.cpp
```
Expected: both show `if (flags != -1)` guard.

**Step 3: Commit**

```bash
git add NetworkModuleTest/Server/ServerEngine/Network/Platforms/LinuxNetworkEngine.cpp
git commit -m "fix: LinuxNetworkEngine — guard fcntl F_SETFL on valid F_GETFL return

fcntl(F_GETFL) returns -1 on error; passing (-1 | O_NONBLOCK) to
F_SETFL sets unexpected flag bits. macOS already checked this;
align Linux to same pattern.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 3: chore — ExecutionQueue.h blank-line formatting

**Problem:** Every statement/block in ExecutionQueue.h has a trailing blank line, making the file ~290 lines instead of ~170. Logic is unchanged.

**Files:**
- Modify: `NetworkModuleTest/Server/ServerEngine/Concurrency/ExecutionQueue.h`

**Step 1: Collapse consecutive blank lines**

The file has a pattern of single blank lines between almost every line. The goal is:
- Keep ONE blank line between logical sections/methods
- Remove blank lines inside method bodies between individual statements
- Keep the existing blank lines between member variable groups

Do this by reading the file and rewriting it with proper C++ formatting (statements on adjacent lines, blank lines only between methods/sections).

**Step 2: Verify line count reduced and no logic changed**

```bash
wc -l NetworkModuleTest/Server/ServerEngine/Concurrency/ExecutionQueue.h
```
Expected: ~170-180 lines (down from ~895).

```bash
grep -c "mNotEmptyMutexCV\|mNotEmptyLFCV_pop\|mNotFullMutexCV\|mNotFullLFCV\|PopMutexWait\|PopLockFreeWait\|TryPushMutex\|TryPushLockFree" NetworkModuleTest/Server/ServerEngine/Concurrency/ExecutionQueue.h
```
Expected: all key identifiers still present.

**Step 3: Commit**

```bash
git add NetworkModuleTest/Server/ServerEngine/Concurrency/ExecutionQueue.h
git commit -m "chore: ExecutionQueue.h — remove excessive blank lines

Every statement had a trailing blank line (artifact of prior editing).
Collapsed to standard C++ formatting. No logic change.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 4: chore — .gitignore for runtime artifacts

**Problem:** `NetworkModuleTest/0306.txt` and `NetworkModuleTest/db_tasks.wal` are untracked runtime files that pollute `git status`.

**Files:**
- Create or modify: `.gitignore` at repo root

**Step 1: Check if .gitignore exists**

```bash
ls /e/MyGitHub/PublicStudy2/.gitignore 2>/dev/null || echo "missing"
```

**Step 2: Add patterns**

Append to `.gitignore`:
```gitignore
# Runtime DB artifacts
NetworkModuleTest/*.wal
NetworkModuleTest/*.txt
```

**Step 3: Verify untracked files disappear**

```bash
git status
```
Expected: `0306.txt` and `db_tasks.wal` no longer listed under "Untracked files".

**Step 4: Commit**

```bash
git add .gitignore
git commit -m "chore: gitignore runtime artifacts (*.wal, *.txt in NetworkModuleTest)

db_tasks.wal and session log .txt files are generated at runtime
and should not be tracked.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Completion Check

After all 4 commits:

```bash
git log --oneline -7
git status
grep -n "mNotEmptyCV[^M]" NetworkModuleTest/Server/ServerEngine/Concurrency/ExecutionQueue.h
```

- Log shows 4 new commits on top of `e88cbe9`
- Status: clean (no untracked, no modified)
- grep: no bare `mNotEmptyCV` references remain
