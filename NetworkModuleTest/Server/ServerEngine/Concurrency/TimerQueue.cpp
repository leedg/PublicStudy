// English: TimerQueue implementation
// 한글: TimerQueue 구현

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

	// English: Wrap one-shot callback so it returns false (no reschedule).
	// 한글: 단발 콜백을 false 반환(재등록 없음) 래퍼로 감쌈.
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
	// English: Notify the worker so it can re-evaluate the heap top immediately.
	//          If the cancelled entry is the next to fire, the worker would otherwise
	//          sleep until its fire time before discovering it is cancelled.
	// 한글: 워커가 힙 최상단을 즉시 재평가할 수 있도록 알림.
	//       취소된 항목이 다음 실행 예정이면, 알림 없이는 워커가 그 시간까지 잠듦.
	mCV.notify_one();
	return true;
}

// =============================================================================
// English: Private helpers
// 한글: 내부 헬퍼
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
	// English: Caller must hold mMutex and mHeap must be non-empty.
	// 한글: 호출자가 mMutex 보유 중이어야 하며 mHeap은 비어있지 않아야 함.
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

		// English: Block until there is at least one entry or the queue shuts down.
		// 한글: 항목이 생기거나 큐가 종료될 때까지 블록.
		mCV.wait(lock, [this] {
			return !mRunning.load(std::memory_order_acquire) || !mHeap.empty();
		});

		// English: On shutdown, discard all pending timers and exit immediately.
		//          Do NOT spin waiting for future-scheduled entries to mature.
		// 한글: 종료 시 미래 항목 포함 모든 대기 타이머를 버리고 즉시 종료.
		//       미래 예약 항목이 성숙하기를 기다리며 스핀하지 않음.
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
			// English: Wait until the earliest scheduled time (or notified earlier).
			// 한글: 가장 이른 예약 시각까지(또는 더 일찍 알림 시) 대기.
			mCV.wait_until(lock, fireTime, [this] {
				return !mRunning.load(std::memory_order_acquire) ||
				       mHeap.empty() ||
				       mHeap.front().nextFire <= std::chrono::steady_clock::now();
			});
			continue; // re-evaluate at top of loop
		}

		// English: Pop the ready entry while holding the lock.
		// 한글: 락 보유 중 준비된 항목 꺼내기.
		TimerEntry entry = PopTop();

		// English: Always erase from cancelled set — prevents stale handle accumulation
		//          when Cancel() is called after a one-shot timer has already fired.
		// 한글: 취소 집합에서 항상 제거 — 원샷 타이머 실행 완료 후 Cancel()이
		//       호출될 때 핸들이 mCancelledHandles에 영구 잔류하는 누수를 방지.
		const bool wasCancelled = mCancelledHandles.erase(entry.handle) > 0;
		if (wasCancelled)
		{
			continue;
		}

		lock.unlock();

		// English: Fire the callback outside the lock.
		// 한글: 락 밖에서 콜백 실행.
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

		// English: Reschedule repeating timers if callback returned true.
		// 한글: 콜백이 true를 반환한 경우 반복 타이머 재등록.
		if (reschedule && entry.intervalMs > 0)
		{
			entry.nextFire = std::chrono::steady_clock::now() +
			                 std::chrono::milliseconds(entry.intervalMs);
			PushEntry(std::move(entry));
		}
	}
}

} // namespace Network::Concurrency
