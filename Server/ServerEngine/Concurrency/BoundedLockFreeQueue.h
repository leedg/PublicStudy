#pragma once

// English: Bounded lock-free MPMC queue (ring buffer).
// 한글: 고정 크기 lock-free MPMC 큐(링 버퍼).

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace Network::Concurrency
{
// =============================================================================
// English: BoundedLockFreeQueue
// - Multi-producer / multi-consumer queue.
// - Capacity is rounded up to power-of-two for index masking.
// - Non-blocking API only: TryEnqueue / TryDequeue.
//
// 한글: BoundedLockFreeQueue
// - 다중 생산자 / 다중 소비자 큐.
// - capacity는 마스킹 연산을 위해 2의 거듭제곱으로 보정.
// - 논블로킹 API만 제공: TryEnqueue / TryDequeue.
// =============================================================================

template <typename T>
class BoundedLockFreeQueue
{
  public:
	explicit BoundedLockFreeQueue(size_t capacity)
		: mCapacity(NormalizeCapacity(capacity)),
		  mMask(mCapacity - 1),
		  mCells(mCapacity),
		  mEnqueuePos(0),
		  mDequeuePos(0)
	{
		static_assert(std::is_move_constructible<T>::value,
					  "BoundedLockFreeQueue requires move-constructible T");

		for (size_t i = 0; i < mCapacity; ++i)
		{
			mCells[i].mSequence.store(i, std::memory_order_relaxed);
		}
	}

	BoundedLockFreeQueue(const BoundedLockFreeQueue &) = delete;
	BoundedLockFreeQueue &operator=(const BoundedLockFreeQueue &) = delete;

	BoundedLockFreeQueue(BoundedLockFreeQueue &&) = delete;
	BoundedLockFreeQueue &operator=(BoundedLockFreeQueue &&) = delete;

	bool TryEnqueue(const T &value)
	{
		T copy(value);
		return TryEnqueue(std::move(copy));
	}

	bool TryEnqueue(T &&value)
	{
		Cell *cell = nullptr;
		size_t pos = mEnqueuePos.load(std::memory_order_relaxed);

		for (;;)
		{
			cell = &mCells[pos & mMask];
			const size_t seq = cell->mSequence.load(std::memory_order_acquire);
			const intptr_t diff =
				static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

			if (diff == 0)
			{
				if (mEnqueuePos.compare_exchange_weak(
						pos, pos + 1, std::memory_order_relaxed))
				{
					break;
				}
			}
			else if (diff < 0)
			{
				// English: Queue full.
				// 한글: 큐가 가득 참.
				return false;
			}
			else
			{
				pos = mEnqueuePos.load(std::memory_order_relaxed);
			}
		}

		cell->mData.emplace(std::move(value));
		cell->mSequence.store(pos + 1, std::memory_order_release);
		return true;
	}

	bool TryDequeue(T &out)
	{
		Cell *cell = nullptr;
		size_t pos = mDequeuePos.load(std::memory_order_relaxed);

		for (;;)
		{
			cell = &mCells[pos & mMask];
			const size_t seq = cell->mSequence.load(std::memory_order_acquire);
			const intptr_t diff =
				static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

			if (diff == 0)
			{
				if (mDequeuePos.compare_exchange_weak(
						pos, pos + 1, std::memory_order_relaxed))
				{
					break;
				}
			}
			else if (diff < 0)
			{
				// English: Queue empty.
				// 한글: 큐가 비어 있음.
				return false;
			}
			else
			{
				pos = mDequeuePos.load(std::memory_order_relaxed);
			}
		}

		out = std::move(*cell->mData);
		cell->mData.reset();
		cell->mSequence.store(pos + mMask + 1, std::memory_order_release);
		return true;
	}

	size_t Capacity() const
	{
		return mCapacity;
	}

  private:
	// English: Each cell is cache-line aligned to prevent false sharing between adjacent slots.
	// 한글: 인접 슬롯 간 false sharing 방지를 위해 셀마다 캐시라인 정렬.
	struct alignas(64) Cell
	{
		std::atomic<size_t> mSequence;
		std::optional<T> mData;
	};

	static size_t NormalizeCapacity(size_t requested)
	{
		// English: Keep minimum 2 to satisfy ring progression.
		// In a ring buffer implementation using enqueue/dequeue position counters
		// with modulo masking, we need at least 2 cells to distinguish between
		// "empty queue" and "full queue" states using the sequence number.
		// With capacity 1, the sequence wraps immediately and we cannot differentiate
		// these states correctly.
		// 한글: 링 진행 보장을 위해 최소 2로 보정.
		// 시퀀스 번호를 사용하여 enqueue/dequeue 포인터를 구분하는 링 버퍼에서는
		// "비어있음"과 "가득참" 상태를 올바르게 구분하기 위해 최소 2개 셀이 필요합니다.
		// 용량이 1이면 시퀀스가 즉시 래핑되어 상태를 올바르게 구분할 수 없습니다.
		size_t value = (requested < 2) ? 2 : requested;
		size_t powerOfTwo = 1;
		while (powerOfTwo < value)
		{
			powerOfTwo <<= 1;
		}
		return powerOfTwo;
	}

	const size_t mCapacity;
	const size_t mMask;
	std::vector<Cell> mCells;

	alignas(64) std::atomic<size_t> mEnqueuePos;
	alignas(64) std::atomic<size_t> mDequeuePos;
};

} // namespace Network::Concurrency
