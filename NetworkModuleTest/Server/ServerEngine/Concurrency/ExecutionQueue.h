#pragma once

// 백프레셔 제어를 지원하는 mutex 기반 실행 큐.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <utility>

namespace Network::Concurrency
{

// =============================================================================
// BackpressurePolicy
//
// RejectNewest: 큐가 가득 찼을 때 새 작업을 즉시 거부하고 false를 반환.
//   - 생산자가 넌블로킹이어야 하는 I/O 스레드(IOCP 완료 포트 등)에 적합.
//   - 단점: 작업 유실 가능. 상위 레이어에서 재시도 또는 에러 처리가 필요.
//
// Block: 큐에 공간이 생길 때까지 생산자를 블로킹(선택적 타임아웃 지원).
//   - 처리 속도보다 생산 속도가 일시적으로 빠를 때 유실 없이 흐름 제어 가능.
//   - 단점: 생산자 스레드가 대기하므로 공유 스레드(워커 풀 등)에서는 주의 필요.
// =============================================================================
enum class BackpressurePolicy : uint8_t
{
	RejectNewest,
	Block,
};

template <typename T>
struct ExecutionQueueOptions
{
	BackpressurePolicy mBackpressure = BackpressurePolicy::RejectNewest;  // 큐 포화 시 동작 정책
	size_t mCapacity = 0;                                                  // 최대 수용 항목 수 (0 = 무제한)
};

// =============================================================================
// ExecutionQueue
//
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
		// 람다 조건식으로 spurious wakeup을 방어한다.
		// OS는 notify 없이도 wait를 깨울 수 있으므로(spurious wakeup),
		// 람다가 false를 반환하면 wait/wait_until이 자동으로 재대기한다.
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

	// PopWait: 생산자의 push+notify와 동일한 뮤텍스(mMutex) 하에서
	// mNotEmptyCV를 대기한다. 덕분에 소비자의 빈 큐 확인과 wait 진입 사이에
	// notify가 끼어드는 missed-notification 경쟁이 원천 차단된다.
	// 람다 조건식이 spurious wakeup 후 재확인을 담당한다.
	bool PopWait(T &out, int timeoutMs)
	{
		const auto deadline =
			std::chrono::steady_clock::now() +
			std::chrono::milliseconds(timeoutMs >= 0 ? timeoutMs : 0);
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
		// shutdown 이후 잔여 아이템 drain: Shutdown() 전에 이미 큐에 들어온 작업을 버리지 않는다.
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

	// ─────────────────────────────────────────────
	// 설정 & 상태
	// ─────────────────────────────────────────────
	ExecutionQueueOptions<T>    mOptions;    // 백프레셔 정책 및 용량 한도
	std::atomic<bool>           mShutdown;  // true → Push/Pop 즉시 거부; acq_rel 쌍으로 가시성 보장
	std::atomic<size_t>         mSize;      // 현재 큐 항목 수; Size()/Empty() 논블로킹 조회용

	// ─────────────────────────────────────────────
	// 저장소 & 동기화
	// ─────────────────────────────────────────────
	std::queue<T>               mQueue;       // 실제 항목을 보관하는 FIFO 컨테이너 (mMutex 보호)
	mutable std::mutex          mMutex;       // mQueue 접근 직렬화
	std::condition_variable     mNotEmptyCV;  // 항목 추가 시 notify → Pop 대기자 깨움
	std::condition_variable     mNotFullCV;   // 항목 제거 시 notify → Block 정책 Push 대기자 깨움
};

} // namespace Network::Concurrency
