// TimerQueue implementation

#include "TimerQueue.h"
#include "Utils/Logger.h"

namespace Network::Concurrency
{

TimerQueue::TimerQueue() = default;

TimerQueue::~TimerQueue()
{
	Shutdown();
}

bool TimerQueue::Initialize()
{
	bool expected = false;
	if (!mRunning.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
	{
		Utils::Logger::Warn("TimerQueue: already running");
		return true;
	}

	mWorkerThread = std::thread(&TimerQueue::WorkerLoop, this);
	Utils::Logger::Info("TimerQueue: initialized");
	return true;
}

void TimerQueue::Shutdown()
{
	bool expected = true;
	if (!mRunning.compare_exchange_strong(expected, false, std::memory_order_acq_rel))
	{
		return;
	}

	mCV.notify_all();

	if (mWorkerThread.joinable())
	{
		mWorkerThread.join();
	}

	{
		std::lock_guard<std::mutex> lock(mMutex);
		mHeap.clear();
		mCancelledHandles.clear();
	}

	Utils::Logger::Info("TimerQueue: shutdown complete");
}

TimerQueue::TimerHandle TimerQueue::ScheduleOnce(TimerCallback cb, uint32_t delayMs)
{
	const TimerHandle handle = mNextHandle.fetch_add(1, std::memory_order_relaxed);

	// Wrap one-shot callback so it returns false (no reschedule).
	auto wrapped = [fn = std::move(cb)]() mutable -> bool
	{
		fn();
		return false;
	};

	TimerEntry entry;
	entry.handle     = handle;
	entry.nextFire   = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs);
	entry.intervalMs = 0;
	entry.cb         = std::move(wrapped);

	PushEntry(std::move(entry));
	return handle;
}

TimerQueue::TimerHandle TimerQueue::ScheduleRepeat(std::function<bool()> cb,
                                                    uint32_t intervalMs)
{
	const TimerHandle handle = mNextHandle.fetch_add(1, std::memory_order_relaxed);

	TimerEntry entry;
	entry.handle     = handle;
	entry.nextFire   = std::chrono::steady_clock::now() + std::chrono::milliseconds(intervalMs);
	entry.intervalMs = intervalMs;
	entry.cb         = std::move(cb);

	PushEntry(std::move(entry));
	return handle;
}

bool TimerQueue::Cancel(TimerHandle handle)
{
	if (handle == 0)
	{
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(mMutex);
		mCancelledHandles.insert(handle);
	}
	// Notify the worker so it can re-evaluate the heap top immediately.
	//          If the cancelled entry is the next to fire, the worker would otherwise
	//          sleep until its fire time before discovering it is cancelled.
	mCV.notify_one();
	return true;
}

// =============================================================================
// Private helpers
// =============================================================================

void TimerQueue::PushEntry(TimerEntry e)
{
	{
		std::lock_guard<std::mutex> lock(mMutex);
		mHeap.push_back(std::move(e));
		std::push_heap(mHeap.begin(), mHeap.end(), EntryCompare{});
	}
	mCV.notify_one();
}

TimerQueue::TimerEntry TimerQueue::PopTop()
{
	// Caller must hold mMutex and mHeap must be non-empty.
	std::pop_heap(mHeap.begin(), mHeap.end(), EntryCompare{});
	TimerEntry e = std::move(mHeap.back());
	mHeap.pop_back();
	return e;
}

void TimerQueue::WorkerLoop()
{
	for (;;)
	{
		std::unique_lock<std::mutex> lock(mMutex);

		// Block until there is at least one entry or the queue shuts down.
		mCV.wait(lock, [this] {
			return !mRunning.load(std::memory_order_acquire) || !mHeap.empty();
		});

		// On shutdown, discard all pending timers and exit immediately.
		//          Do NOT spin waiting for future-scheduled entries to mature.
		if (!mRunning.load(std::memory_order_acquire))
		{
			break;
		}

		if (mHeap.empty())
		{
			continue;
		}

		const auto now      = std::chrono::steady_clock::now();
		const auto fireTime = mHeap.front().nextFire;

		if (fireTime > now)
		{
			// Wait until the earliest scheduled time (or notified earlier).
			mCV.wait_until(lock, fireTime, [this] {
				return !mRunning.load(std::memory_order_acquire) ||
				       mHeap.empty() ||
				       mHeap.front().nextFire <= std::chrono::steady_clock::now();
			});
			continue; // re-evaluate at top of loop
		}

		// Pop the ready entry while holding the lock.
		TimerEntry entry = PopTop();

		// Always erase from cancelled set — prevents stale handle accumulation
		//          when Cancel() is called after a one-shot timer has already fired.
		const bool wasCancelled = mCancelledHandles.erase(entry.handle) > 0;
		if (wasCancelled)
		{
			continue;
		}

		lock.unlock();

		// Fire the callback outside the lock.
		bool reschedule = false;
		try
		{
			reschedule = entry.cb();
		}
		catch (const std::exception &ex)
		{
			Utils::Logger::Error("TimerQueue: callback exception: " + std::string(ex.what()));
		}
		catch (...)
		{
			Utils::Logger::Error("TimerQueue: callback unknown exception");
		}

		// Reschedule repeating timers if callback returned true.
		if (reschedule && entry.intervalMs > 0)
		{
			entry.nextFire = std::chrono::steady_clock::now() +
			                 std::chrono::milliseconds(entry.intervalMs);
			PushEntry(std::move(entry));
		}
	}
}

} // namespace Network::Concurrency
