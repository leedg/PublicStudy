#pragma once

// English: Single-threaded min-heap timer queue for periodic/once callbacks.
// 한글: 주기적/단발 콜백을 위한 단일 스레드 min-heap 타이머 큐.
//
// Design:
//   - One background worker thread (std::thread).
//   - Callbacks fire on that thread; keep them short or offload to a pool.
//   - ScheduleRepeat: callback returns bool (true = reschedule, false = auto-cancel).
//   - Cancel(): marks handle as cancelled; safe to call concurrently.
//
// 설계:
//   - 단일 백그라운드 워커 스레드(std::thread).
//   - 콜백은 워커 스레드에서 실행; 짧게 유지하거나 풀로 오프로드.
//   - ScheduleRepeat: 콜백이 bool 반환 (true = 재등록, false = 자동 해제).
//   - Cancel(): 핸들을 취소 표시; 동시 호출 안전.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

namespace Network::Concurrency
{

class TimerQueue
{
  public:
	using TimerHandle = uint64_t;
	using TimerCallback = std::function<void()>;

	TimerQueue();
	~TimerQueue();

	TimerQueue(const TimerQueue &) = delete;
	TimerQueue &operator=(const TimerQueue &) = delete;

	// English: Start the background worker thread.
	// 한글: 백그라운드 워커 스레드 시작.
	bool Initialize();

	// English: Stop the worker thread and drain all pending entries.
	// 한글: 워커 스레드 정지 및 대기 중인 모든 항목 드레인.
	void Shutdown();

	// English: Schedule a one-shot callback after delayMs milliseconds.
	// 한글: delayMs 밀리초 후 콜백 1회 실행.
	TimerHandle ScheduleOnce(TimerCallback cb, uint32_t delayMs);

	// English: Schedule a repeating callback every intervalMs milliseconds.
	//          The callback must return true to reschedule itself, or false to stop.
	// 한글: intervalMs 마다 콜백 반복 실행.
	//       콜백이 true를 반환하면 재등록, false를 반환하면 자동 해제.
	TimerHandle ScheduleRepeat(std::function<bool()> cb, uint32_t intervalMs);

	// English: Cancel a previously scheduled timer. No-op if already fired or not found.
	//          Safe to call concurrently with callbacks in flight.
	// 한글: 이전에 등록한 타이머 취소. 이미 실행됐거나 없으면 no-op.
	//       실행 중인 콜백과 동시 호출 안전.
	bool Cancel(TimerHandle handle);

	bool IsRunning() const
	{
		return mRunning.load(std::memory_order_acquire);
	}

  private:
	// ─── Internal entry ────────────────────────────────────────────────────────
	struct TimerEntry
	{
		TimerHandle handle{0};
		std::chrono::steady_clock::time_point nextFire;
		uint32_t intervalMs{0}; // English: 0 = once / 한글: 0 = 단발
		std::function<bool()> cb;
	};

	// English: Min-heap comparator: entry with earliest fire time has highest priority.
	// 한글: min-heap 비교자: 가장 이른 실행 시각이 최우선.
	struct EntryCompare
	{
		bool operator()(const TimerEntry &a, const TimerEntry &b) const
		{
			return a.nextFire > b.nextFire;
		}
	};

	// English: Push an entry onto the heap and notify the worker.
	// 한글: 힙에 항목 삽입 후 워커 알림.
	void PushEntry(TimerEntry e);

	// English: Pop the top (earliest) entry. Caller must hold mMutex and heap non-empty.
	// 한글: 최상단(가장 이른) 항목 꺼내기. 호출자가 mMutex 보유 중이어야 하며 힙 비어있지 않아야 함.
	TimerEntry PopTop();

	void WorkerLoop();

	// English: Vector-based min-heap (std::push_heap / std::pop_heap).
	// 한글: 벡터 기반 min-heap (std::push_heap / std::pop_heap).
	std::vector<TimerEntry> mHeap;
	mutable std::mutex mMutex;
	std::condition_variable mCV;
	std::thread mWorkerThread;
	std::atomic<bool> mRunning{false};
	std::atomic<TimerHandle> mNextHandle{1};

	// English: Handles cancelled between pop and fire (protected by mMutex).
	// 한글: 꺼내기와 실행 사이에 취소된 핸들 집합 (mMutex 보호).
	std::unordered_set<TimerHandle> mCancelledHandles;
};

} // namespace Network::Concurrency
