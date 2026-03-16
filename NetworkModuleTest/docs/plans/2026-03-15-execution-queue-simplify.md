# ExecutionQueue Simplify & Docs Update Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Remove the lock-free backend from ExecutionQueue (mutex-only), delete BoundedLockFreeQueue.h, update all draw.io diagrams so every image in the report has a draw.io source, and regenerate the docx as v1.4.

**Architecture:** Pure simplification — no behavior change for the default (mutex) path. ExecutionQueue.h shrinks from 533 → ~150 lines. All draw.io diagram files must exist in ExecutiveSummary/assets/ and Network_Async_DB_Report_img_B/ for every image the report uses.

**Tech Stack:** C++17, MSBuild/VS2022, python-docx, draw.io XML

---

## Scope

| Item | Decision |
|------|----------|
| `QueueBackend` enum | **Remove** |
| `BoundedLockFreeQueue.h` | **Delete** (only used by ExecutionQueue.h) |
| `mBackend` in `ExecutionQueueOptions` | **Remove** |
| All lock-free private methods | **Remove** |
| `mWaitMutex`, `mLockFreeQueue`, LF CVs | **Remove** |
| `#ifdef NETWORK_ORDERED_TASKQUEUE_LOCKFREE` | **Remove** |
| Mutex path logic | **Keep as-is** |
| `diag_async_3_execqueue.drawio` | **Update** — mutex-only diagram |
| `03-client-lifecycle-sequence.drawio` | **Create** in ExecutiveSummary/assets/ |
| `04-async-db-flow-sequence.drawio` | **Create** in ExecutiveSummary/assets/ |
| Report version | **1.3 → 1.4** |

---

## Build command

```powershell
powershell.exe -Command "& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe' 'E:\MyGitHub\PublicStudy\NetworkModuleTest\NetworkModuleTest.sln' /p:Configuration=Debug /p:Platform=x64 /m /nologo 2>&1 | Select-Object -Last 10"
```

Target: **0 errors, 0 warnings**

---

## Task 1 — Rewrite ExecutionQueue.h (mutex-only)

**File:** `Server/ServerEngine/Concurrency/ExecutionQueue.h`

Replace the entire file with the following (533 lines → ~155 lines):

```cpp
#pragma once

// Mutex-backed execution queue with backpressure control.
// 한글: 백프레셔 제어를 지원하는 mutex 기반 실행 큐.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <utility>

namespace Network::Concurrency
{

enum class BackpressurePolicy : uint8_t
{
	RejectNewest,
	Block,
};

template <typename T>
struct ExecutionQueueOptions
{
	BackpressurePolicy mBackpressure = BackpressurePolicy::RejectNewest;

	size_t mCapacity = 0; // 0 = unbounded
	                      // 한글: 0 = 무제한
};

// =============================================================================
// ExecutionQueue
// - TryPush/TryPop are always non-blocking.
// - Push/Pop block when BackpressurePolicy::Block and the queue is full.
//
// 한글: ExecutionQueue
// - TryPush/TryPop은 항상 논블로킹.
// - Push/Pop은 BackpressurePolicy::Block이고 큐가 가득 찼을 때 블로킹.
// =============================================================================
template <typename T>
class ExecutionQueue
{
  public:

	explicit ExecutionQueue(const ExecutionQueueOptions<T> &options)
		: mOptions(options), mShutdown(false), mSize(0)
	{
	}

	ExecutionQueue(const ExecutionQueue &) = delete;
	ExecutionQueue &operator=(const ExecutionQueue &) = delete;

	bool TryPush(const T &value)
	{
		T copy(value);
		return TryPush(std::move(copy));
	}

	bool TryPush(T &&value)
	{
		if (mShutdown.load(std::memory_order_acquire))
			return false;
		return TryPushImpl(std::move(value));
	}

	bool Push(const T &value, int timeoutMs = -1)
	{
		T copy(value);
		return Push(std::move(copy), timeoutMs);
	}

	bool Push(T &&value, int timeoutMs = -1)
	{
		if (mOptions.mBackpressure == BackpressurePolicy::RejectNewest)
			return TryPush(std::move(value));
		return PushBlocking(std::move(value), timeoutMs);
	}

	bool TryPop(T &out)
	{
		std::lock_guard<std::mutex> lock(mMutex);
		if (mQueue.empty())
			return false;
		out = std::move(mQueue.front());
		mQueue.pop();
		mSize.fetch_sub(1, std::memory_order_release);
		mNotFullCV.notify_one();
		return true;
	}

	bool Pop(T &out, int timeoutMs = -1)
	{
		if (TryPop(out))
			return true;
		if (timeoutMs == 0)
			return false;
		return PopWait(out, timeoutMs);
	}

	void Shutdown()
	{
		bool expected = false;
		if (!mShutdown.compare_exchange_strong(
				expected, true, std::memory_order_acq_rel))
			return;
		mNotEmptyCV.notify_all();
		mNotFullCV.notify_all();
	}

	bool IsShutdown() const { return mShutdown.load(std::memory_order_acquire); }
	size_t Size()      const { return mSize.load(std::memory_order_acquire); }
	bool   Empty()     const { return Size() == 0; }
	size_t Capacity()  const { return mOptions.mCapacity; }

  private:

	bool TryPushImpl(T &&value)
	{
		{
			std::lock_guard<std::mutex> lock(mMutex);
			if (mShutdown.load(std::memory_order_acquire))
				return false;
			if (mOptions.mCapacity > 0 && mQueue.size() >= mOptions.mCapacity)
				return false;
			mQueue.push(std::move(value));
			mSize.fetch_add(1, std::memory_order_release);
		}
		mNotEmptyCV.notify_one();
		return true;
	}

	bool PushBlocking(T &&value, int timeoutMs)
	{
		std::unique_lock<std::mutex> lock(mMutex);
		auto canPush = [this] {
			return mShutdown.load(std::memory_order_acquire) ||
			       mOptions.mCapacity == 0 ||
			       mQueue.size() < mOptions.mCapacity;
		};
		if (timeoutMs < 0)
		{
			mNotFullCV.wait(lock, canPush);
		}
		else
		{
			const auto deadline = std::chrono::steady_clock::now() +
			                      std::chrono::milliseconds(timeoutMs);
			if (!mNotFullCV.wait_until(lock, deadline, canPush))
				return false;
		}
		if (mShutdown.load(std::memory_order_acquire))
			return false;
		mQueue.push(std::move(value));
		mSize.fetch_add(1, std::memory_order_release);
		lock.unlock();
		mNotEmptyCV.notify_one();
		return true;
	}

	// PopWait: waits mNotEmptyCV under mMutex — the same mutex held by the
	//          producer during push+notify — so notify cannot slip between the
	//          consumer's empty-check and its wait entry.
	// 한글: PopWait: 생산자가 push+notify 시 보유하는 것과 동일한 뮤텍스(mMutex) 하에서
	//       mNotEmptyCV를 대기하여 missed-notification 경쟁을 제거함.
	bool PopWait(T &out, int timeoutMs)
	{
		const auto deadline =
			(timeoutMs < 0)
				? std::chrono::steady_clock::time_point::max()
				: std::chrono::steady_clock::now() +
				  std::chrono::milliseconds(timeoutMs);
		while (!mShutdown.load(std::memory_order_acquire))
		{
			std::unique_lock<std::mutex> lock(mMutex);
			auto hasItem = [this] {
				return mShutdown.load(std::memory_order_acquire) ||
				       !mQueue.empty();
			};
			if (timeoutMs < 0)
				mNotEmptyCV.wait(lock, hasItem);
			else if (!mNotEmptyCV.wait_until(lock, deadline, hasItem))
				return false;

			if (mShutdown.load(std::memory_order_acquire))
				break;
			if (!mQueue.empty())
			{
				out = std::move(mQueue.front());
				mQueue.pop();
				mSize.fetch_sub(1, std::memory_order_release);
				lock.unlock();
				mNotFullCV.notify_one();
				return true;
			}
		}
		// After shutdown, drain remaining items.
		// 한글: shutdown 이후 잔여 아이템 drain.
		std::lock_guard<std::mutex> lock(mMutex);
		if (!mQueue.empty())
		{
			out = std::move(mQueue.front());
			mQueue.pop();
			mSize.fetch_sub(1, std::memory_order_release);
			return true;
		}
		return false;
	}

	ExecutionQueueOptions<T>    mOptions;
	std::atomic<bool>           mShutdown;
	std::atomic<size_t>         mSize;
	std::queue<T>               mQueue;
	mutable std::mutex          mMutex;
	std::condition_variable     mNotEmptyCV;
	std::condition_variable     mNotFullCV;
};

} // namespace Network::Concurrency
```

**Commit:**
```bash
git add Server/ServerEngine/Concurrency/ExecutionQueue.h
git commit -m "refactor: simplify ExecutionQueue to mutex-only backend"
```

---

## Task 2 — Update OrderedTaskQueue.cpp

**File:** `Server/DBServer/src/OrderedTaskQueue.cpp`

In `Initialize()`, replace the backend-selection block:

```cpp
// BEFORE (lines ~48-57)
#ifdef NETWORK_ORDERED_TASKQUEUE_LOCKFREE
        options.mQueueOptions.mBackend = Network::Concurrency::QueueBackend::LockFree;
        options.mQueueOptions.mCapacity = 8192;
        Logger::Info("OrderedTaskQueue: lock-free backend enabled");
#else
        // Default to mutex backend for predictable behavior.
        // 한글: 기본은 예측 가능한 동작을 위해 mutex 백엔드 사용.
        options.mQueueOptions.mBackend = Network::Concurrency::QueueBackend::Mutex;
        options.mQueueOptions.mCapacity = 8192;
#endif

// AFTER
        options.mQueueOptions.mCapacity = 8192;
```

**Commit:**
```bash
git add Server/DBServer/src/OrderedTaskQueue.cpp
git commit -m "refactor: remove lock-free backend selection from OrderedTaskQueue"
```

---

## Task 3 — Delete BoundedLockFreeQueue.h + remove from vcxproj

**Files:**
- Delete: `Server/ServerEngine/Concurrency/BoundedLockFreeQueue.h`
- Modify: `Server/ServerEngine/ServerEngine.vcxproj` (line 221)
- Modify: `Server/ServerEngine/ServerEngine.vcxproj.filters` (lines 334-336)

**Step 1:** Delete `BoundedLockFreeQueue.h`

**Step 2:** In `ServerEngine.vcxproj`, remove:
```xml
    <ClInclude Include="Concurrency\BoundedLockFreeQueue.h" />
```

**Step 3:** In `ServerEngine.vcxproj.filters`, remove:
```xml
    <ClInclude Include="Concurrency\BoundedLockFreeQueue.h">
      <Filter>Concurrency</Filter>
    </ClInclude>
```

**Step 4: Build**
```powershell
powershell.exe -Command "& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe' 'E:\MyGitHub\PublicStudy\NetworkModuleTest\NetworkModuleTest.sln' /p:Configuration=Debug /p:Platform=x64 /m /nologo 2>&1 | Select-Object -Last 10"
```
Expected: 0 errors, 0 warnings.

**Step 5: Commit**
```bash
git add Server/ServerEngine/Concurrency/BoundedLockFreeQueue.h
git add Server/ServerEngine/ServerEngine.vcxproj
git add Server/ServerEngine/ServerEngine.vcxproj.filters
git commit -m "refactor: delete BoundedLockFreeQueue.h — no longer needed after mutex-only simplification"
```

---

## Task 4 — Update diag_async_3_execqueue draw.io (all folders)

**Update** the following files with the new mutex-only ExecutionQueue diagram:
- `Doc/Reports/Network_Async_DB_Report_img_B/diag_async_3_execqueue.drawio`
- `Doc/Reports/Network_Async_DB_Report_img/diag_async_3_execqueue.drawio`
- `Doc/Reports/Network_Async_DB_Report_img_C/diag_async_3_execqueue.drawio`

New diagram shows: push flow (lock→push→unlock→notify), pop flow (wait_until→pop→notify), members (queue, mutex, 2×CV, atomic), capacity & backpressure label. NO lock-free section.

Write this XML to all three files:

```xml
<?xml version='1.0' encoding='utf-8'?>
<mxfile host="app.diagrams.net">
  <diagram name="ExecutionQueue (Mutex)">
    <mxGraphModel dx="1422" dy="794" grid="1" gridSize="10" page="1" pageWidth="1920" pageHeight="1080">
      <root>
        <mxCell id="0" />
        <mxCell id="1" parent="0" />

        <!-- Title -->
        <mxCell id="title" value="ExecutionQueue&lt;T&gt; — Mutex Backend"
          style="text;html=1;strokeColor=none;fillColor=#1f3d63;fontColor=#ffffff;
                 fontSize=22;fontStyle=1;align=center;spacingTop=6;"
          vertex="1" parent="1">
          <mxGeometry x="200" y="30" width="900" height="50" as="geometry" />
        </mxCell>

        <!-- Member box -->
        <mxCell id="members" value="Members"
          style="swimlane;fontStyle=1;startSize=30;fillColor=#dae8fc;strokeColor=#6c8ebf;fontSize=14;"
          vertex="1" parent="1">
          <mxGeometry x="200" y="110" width="360" height="260" as="geometry" />
        </mxCell>
        <mxCell id="m1" value="std::queue&lt;T&gt;  mQueue"
          style="text;align=left;spacingLeft=8;fontSize=13;" vertex="1" parent="members">
          <mxGeometry x="0" y="30" width="360" height="26" as="geometry" />
        </mxCell>
        <mxCell id="m2" value="std::mutex  mMutex"
          style="text;align=left;spacingLeft=8;fontSize=13;" vertex="1" parent="members">
          <mxGeometry x="0" y="60" width="360" height="26" as="geometry" />
        </mxCell>
        <mxCell id="m3" value="condition_variable  mNotEmptyCV"
          style="text;align=left;spacingLeft=8;fontSize=13;" vertex="1" parent="members">
          <mxGeometry x="0" y="90" width="360" height="26" as="geometry" />
        </mxCell>
        <mxCell id="m4" value="condition_variable  mNotFullCV"
          style="text;align=left;spacingLeft=8;fontSize=13;" vertex="1" parent="members">
          <mxGeometry x="0" y="120" width="360" height="26" as="geometry" />
        </mxCell>
        <mxCell id="m5" value="atomic&lt;bool&gt;  mShutdown"
          style="text;align=left;spacingLeft=8;fontSize=13;" vertex="1" parent="members">
          <mxGeometry x="0" y="150" width="360" height="26" as="geometry" />
        </mxCell>
        <mxCell id="m6" value="atomic&lt;size_t&gt;  mSize"
          style="text;align=left;spacingLeft=8;fontSize=13;" vertex="1" parent="members">
          <mxGeometry x="0" y="180" width="360" height="26" as="geometry" />
        </mxCell>
        <mxCell id="m7" value="size_t  mCapacity  (0 = unbounded)"
          style="text;align=left;spacingLeft=8;fontSize=13;fontColor=#666666;" vertex="1" parent="members">
          <mxGeometry x="0" y="210" width="360" height="26" as="geometry" />
        </mxCell>

        <!-- Push flow -->
        <mxCell id="push_box" value="Push (producer)"
          style="swimlane;fontStyle=1;startSize=30;fillColor=#d5e8d4;strokeColor=#82b366;fontSize=14;"
          vertex="1" parent="1">
          <mxGeometry x="620" y="110" width="460" height="260" as="geometry" />
        </mxCell>
        <mxCell id="p1" value="1. lock_guard&lt;mutex&gt;(mMutex)"
          style="rounded=1;whiteSpace=wrap;html=1;fillColor=#d5e8d4;strokeColor=#82b366;fontSize=13;"
          vertex="1" parent="push_box">
          <mxGeometry x="20" y="40" width="420" height="36" as="geometry" />
        </mxCell>
        <mxCell id="p2" value="2. capacity check → push(value)"
          style="rounded=1;whiteSpace=wrap;html=1;fillColor=#d5e8d4;strokeColor=#82b366;fontSize=13;"
          vertex="1" parent="push_box">
          <mxGeometry x="20" y="90" width="420" height="36" as="geometry" />
        </mxCell>
        <mxCell id="p3" value="3. mSize.fetch_add(1)"
          style="rounded=1;whiteSpace=wrap;html=1;fillColor=#d5e8d4;strokeColor=#82b366;fontSize=13;"
          vertex="1" parent="push_box">
          <mxGeometry x="20" y="140" width="420" height="36" as="geometry" />
        </mxCell>
        <mxCell id="p4" value="4. unlock → mNotEmptyCV.notify_one()"
          style="rounded=1;whiteSpace=wrap;html=1;fillColor=#82b366;strokeColor=#5a8a40;fontColor=#ffffff;fontSize=13;"
          vertex="1" parent="push_box">
          <mxGeometry x="20" y="190" width="420" height="36" as="geometry" />
        </mxCell>

        <!-- Pop flow -->
        <mxCell id="pop_box" value="Pop (consumer)"
          style="swimlane;fontStyle=1;startSize=30;fillColor=#ffe6cc;strokeColor=#d79b00;fontSize=14;"
          vertex="1" parent="1">
          <mxGeometry x="620" y="400" width="460" height="300" as="geometry" />
        </mxCell>
        <mxCell id="q1" value="1. unique_lock&lt;mutex&gt;(mMutex)"
          style="rounded=1;whiteSpace=wrap;html=1;fillColor=#ffe6cc;strokeColor=#d79b00;fontSize=13;"
          vertex="1" parent="pop_box">
          <mxGeometry x="20" y="40" width="420" height="36" as="geometry" />
        </mxCell>
        <mxCell id="q2" value="2. mNotEmptyCV.wait_until(lock, deadline,&#xa;   []{!mQueue.empty()})"
          style="rounded=1;whiteSpace=wrap;html=1;fillColor=#ffe6cc;strokeColor=#d79b00;fontSize=13;"
          vertex="1" parent="pop_box">
          <mxGeometry x="20" y="90" width="420" height="48" as="geometry" />
        </mxCell>
        <mxCell id="q3" value="3. front() → pop() → mSize.fetch_sub(1)"
          style="rounded=1;whiteSpace=wrap;html=1;fillColor=#ffe6cc;strokeColor=#d79b00;fontSize=13;"
          vertex="1" parent="pop_box">
          <mxGeometry x="20" y="152" width="420" height="36" as="geometry" />
        </mxCell>
        <mxCell id="q4" value="4. unlock → mNotFullCV.notify_one()"
          style="rounded=1;whiteSpace=wrap;html=1;fillColor=#d79b00;strokeColor=#a05900;fontColor=#ffffff;fontSize=13;"
          vertex="1" parent="pop_box">
          <mxGeometry x="20" y="200" width="420" height="36" as="geometry" />
        </mxCell>
        <mxCell id="q5" value="★ notify under same mutex as push → no missed-notification"
          style="text;html=1;align=center;fontStyle=2;fontSize=12;fontColor=#a05900;"
          vertex="1" parent="pop_box">
          <mxGeometry x="20" y="248" width="420" height="30" as="geometry" />
        </mxCell>

        <!-- Backpressure note -->
        <mxCell id="bp" value="BackpressurePolicy&#xa;RejectNewest → TryPush (non-blocking)&#xa;Block → PushBlocking (waits mNotFullCV)"
          style="rounded=1;whiteSpace=wrap;html=1;fillColor=#fff2cc;strokeColor=#d6b656;fontSize=13;align=left;spacingLeft=10;"
          vertex="1" parent="1">
          <mxGeometry x="200" y="400" width="360" height="90" as="geometry" />
        </mxCell>

        <!-- Shutdown note -->
        <mxCell id="sd" value="Shutdown: mShutdown=true → notify_all(NotEmptyCV, NotFullCV)&#xa;PopWait drains remaining items after shutdown."
          style="rounded=1;whiteSpace=wrap;html=1;fillColor=#f8cecc;strokeColor=#b85450;fontSize=13;align=left;spacingLeft=10;"
          vertex="1" parent="1">
          <mxGeometry x="200" y="510" width="360" height="70" as="geometry" />
        </mxCell>

        <!-- Arrow push→queue -->
        <mxCell id="arr1" value="" style="endArrow=block;endFill=1;html=1;strokeWidth=2;strokeColor=#82b366;"
          edge="1" parent="1">
          <mxGeometry relative="1" as="geometry">
            <mxPoint x="560" y="240" as="sourcePoint" />
            <mxPoint x="620" y="240" as="targetPoint" />
          </mxGeometry>
        </mxCell>

        <!-- Arrow queue→pop -->
        <mxCell id="arr2" value="" style="endArrow=block;endFill=1;html=1;strokeWidth=2;strokeColor=#d79b00;"
          edge="1" parent="1">
          <mxGeometry relative="1" as="geometry">
            <mxPoint x="620" y="520" as="sourcePoint" />
            <mxPoint x="560" y="370" as="targetPoint" />
          </mxGeometry>
        </mxCell>

      </root>
    </mxGraphModel>
  </diagram>
</mxfile>
```

**Commit:**
```bash
git add Doc/Reports/Network_Async_DB_Report_img_B/diag_async_3_execqueue.drawio
git add Doc/Reports/Network_Async_DB_Report_img/diag_async_3_execqueue.drawio
git add Doc/Reports/Network_Async_DB_Report_img_C/diag_async_3_execqueue.drawio
git commit -m "docs: update diag_async_3_execqueue — remove lock-free section, mutex-only"
```

---

## Task 5 — Create missing ExecutiveSummary draw.io files

**Create** these two files in `Doc/Reports/ExecutiveSummary/assets/`:

### 03-client-lifecycle-sequence.drawio

Based on the existing `Network_Async_DB_Report_img_B/client_lifecycle_sequence.drawio`.
Adapt it to the ExecutiveSummary style (same content, ensure no overlapping elements, good spacing).

Key sequence (7 actors):
- TestClient → PlatformEngine → SessionManager → ClientSession → ClientPacketHandler → DBTaskQueue → LocalDB
- Flow: TCP connect → accept → CreateSession → OnClientConnected → RecordConnectTime (INSERT SessionConnectLog)
- Then: ProcessRawRecv → ParsePacket → HandlePingRequest → PongResponse
- Parallel: DBTaskQueue worker → LocalDB INSERT

Write a clean draw.io XML with:
- Participants as rounded rectangles (fillColor=#dae8fc, strokeColor=#6c8ebf) spaced 200px apart
- Lifelines as vertical dashed lines
- Messages as horizontal block arrows with labels
- Page size: 1920×1200 to avoid overlap
- Clear vertical spacing (80px per message step)

### 04-async-db-flow-sequence.drawio

Based on `Network_Async_DB_Report_img_B/db_processing_dbtaskqueue.drawio`.
Sequence/architecture hybrid showing:
- Actors: LogicThread, DBTaskQueue, WAL, WorkerThread, IDatabase
- Flow: EnqueueTask → WAL write(Pending) → worker dequeue → Execute → WAL write(Done) → DB commit
- Crash recovery note box

Write clean draw.io XML with same style guidelines.

**Commit:**
```bash
git add Doc/Reports/ExecutiveSummary/assets/03-client-lifecycle-sequence.drawio
git add Doc/Reports/ExecutiveSummary/assets/04-async-db-flow-sequence.drawio
git commit -m "docs: add missing draw.io sources for 03/04 ExecutiveSummary diagrams"
```

---

## Task 6 — Update execution_queue_dual_backend draw.io files

**Update** (rename content, keep filename for compatibility):
- `Doc/Reports/Network_Async_DB_Report_img_B/execution_queue_dual_backend.drawio`
- `Doc/Reports/Network_Async_DB_Report_img/execution_queue_dual_backend.drawio`

Replace content with the new mutex-only design (same XML as Task 4 but titled "ExecutionQueue — Mutex Backend (simplified)").

**Commit:**
```bash
git add Doc/Reports/Network_Async_DB_Report_img_B/execution_queue_dual_backend.drawio
git add Doc/Reports/Network_Async_DB_Report_img/execution_queue_dual_backend.drawio
git commit -m "docs: update execution_queue_dual_backend drawio — mutex-only"
```

---

## Task 7 — Update report script + regenerate docx

**File:** `Doc/Reports/_scripts/Build-NetworkAsyncDB-Report.py`

**Step 1:** Update version and caption.

Find:
```python
("버전",           "1.3"),
```
Replace with:
```python
("버전",           "1.4"),
```

Find:
```python
    image_drawio(doc, "diag_async_3_execqueue.png",
                 caption="ExecutionQueue 이중 백엔드 — Mutex / Lock-Free (draw.io)", style=DRAWIO_STYLE)
```
Replace with:
```python
    image_drawio(doc, "diag_async_3_execqueue.png",
                 caption="ExecutionQueue — Mutex 단일 백엔드 (draw.io)", style=DRAWIO_STYLE)
```

**Step 2:** Add changelog entry.

Find the `section_changelog` rows list and prepend:
```python
            ["2026-03-15",
             "ExecutionQueue 단순화(v1.4): lock-free 백엔드 제거, BoundedLockFreeQueue 삭제,\n"
             "mutex 단일 백엔드로 통일 (533→155줄). draw.io 다이어그램 전면 갱신."],
```

**Step 3:** Regenerate docx
```powershell
cd "E:\MyGitHub\PublicStudy\NetworkModuleTest\Doc\Reports\_scripts"
python Build-NetworkAsyncDB-Report.py
```
Expected: `Network_Async_DB_Report.docx` updated.

**Step 4: Commit**
```bash
git add Doc/Reports/_scripts/Build-NetworkAsyncDB-Report.py
git add Doc/Reports/Network_Async_DB_Report.docx
git commit -m "docs: v1.4 — ExecutionQueue 단순화 반영, draw.io 다이어그램 전면 갱신"
```

---

## Task 8 — Final push

```bash
git log --oneline -8
git push origin main
```
