// TimerQueue 구현

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

	// 단발 콜백을 bool 반환 래퍼로 감싼다. false를 반환하여 재등록을 막는다.
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
	// 취소된 항목이 힙 최상단인 경우 워커가 해당 fire time까지 잠자지 않도록
	// 즉시 깨워 힙 최상단을 재평가하게 한다.
	mCV.notify_one();
	return true;
}

// =============================================================================
// 내부 헬퍼
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
	// 호출자가 mMutex를 보유 중이어야 하며 mHeap은 비어있지 않아야 한다.
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

		// 항목이 생기거나 큐가 종료될 때까지 블록한다.
		// 람다 조건식이 spurious wakeup을 방어한다.
		mCV.wait(lock, [this] {
			return !mRunning.load(std::memory_order_acquire) || !mHeap.empty();
		});

		// 종료 시 미래 예약 항목을 포함해 모든 대기 타이머를 버리고 즉시 종료한다.
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
			// 가장 이른 예약 시각까지 대기한다. 새 항목 삽입이나 Cancel() 알림이
			// 오면 더 일찍 깨어나 루프 상단에서 재평가한다.
			mCV.wait_until(lock, fireTime, [this] {
				return !mRunning.load(std::memory_order_acquire) ||
				       mHeap.empty() ||
				       mHeap.front().nextFire <= std::chrono::steady_clock::now();
			});
			continue; // re-evaluate at top of loop
		}

		// 락을 보유한 상태로 준비된 항목을 꺼낸다.
		TimerEntry entry = PopTop();

		// 취소 집합에서 항상 제거한다. 원샷 타이머 실행 완료 후 Cancel()이
		// 호출되어도 핸들이 mCancelledHandles에 영구 잔류하지 않도록 한다.
		const bool wasCancelled = mCancelledHandles.erase(entry.handle) > 0;
		if (wasCancelled)
		{
			continue;
		}

		lock.unlock();

		// 락 밖에서 콜백을 실행하여 워커 스레드의 lock 점유 시간을 최소화한다.
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

		// 콜백이 true를 반환한 경우 반복 타이머를 재등록한다.
		if (reschedule && entry.intervalMs > 0)
		{
			entry.nextFire = std::chrono::steady_clock::now() +
			                 std::chrono::milliseconds(entry.intervalMs);
			PushEntry(std::move(entry));
		}
	}
}

} // namespace Network::Concurrency
