#pragma once

// Single-threaded min-heap timer queue for periodic/once callbacks.
//
// Design:
//   - One background worker thread (std::thread).
//   - Callbacks fire on that thread; keep them short or offload to a pool.
//   - ScheduleRepeat: callback returns bool (true = reschedule, false = auto-cancel).
//   - Cancel(): marks handle as cancelled; safe to call concurrently.
//

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

	// Start the background worker thread.
	bool Initialize();

	// Stop the worker thread and drain all pending entries.
	void Shutdown();

	// Schedule a one-shot callback after delayMs milliseconds.
	TimerHandle ScheduleOnce(TimerCallback cb, uint32_t delayMs);

	// Schedule a repeating callback every intervalMs milliseconds.
	//          The callback must return true to reschedule itself, or false to stop.
	TimerHandle ScheduleRepeat(std::function<bool()> cb, uint32_t intervalMs);

	// Cancel a previously scheduled timer. No-op if already fired or not found.
	//          Safe to call concurrently with callbacks in flight.
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
		uint32_t intervalMs{0}; // 0 = once
		std::function<bool()> cb;
	};

	// Min-heap comparator: entry with earliest fire time has highest priority.
	struct EntryCompare
	{
		bool operator()(const TimerEntry &a, const TimerEntry &b) const
		{
			return a.nextFire > b.nextFire;
		}
	};

	// Push an entry onto the heap and notify the worker.
	void PushEntry(TimerEntry e);

	// Pop the top (earliest) entry. Caller must hold mMutex and heap non-empty.
	TimerEntry PopTop();

	void WorkerLoop();

	// Vector-based min-heap (std::push_heap / std::pop_heap).
	std::vector<TimerEntry> mHeap;
	mutable std::mutex mMutex;
	std::condition_variable mCV;
	std::thread mWorkerThread;
	std::atomic<bool> mRunning{false};
	std::atomic<TimerHandle> mNextHandle{1};

	// Handles cancelled between pop and fire (protected by mMutex).
	std::unordered_set<TimerHandle> mCancelledHandles;
};

} // namespace Network::Concurrency
