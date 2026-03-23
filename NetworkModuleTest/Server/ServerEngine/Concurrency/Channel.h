#pragma once

// ExecutionQueue<T>를 감싼 타입 기반 생산자/소비자 채널.

#include "ExecutionQueue.h"
#include <utility>

namespace Network::Concurrency
{
// =============================================================================
// Channel<T>
//
// ExecutionQueue<T>의 Push/Pop API를 Send/Receive 의미론으로 래핑한다.
// 백프레셔 정책(RejectNewest / Block)과 용량 제한은 Options를 통해 설정한다.
// =============================================================================

template <typename T>
class Channel
{
  public:
	using Options = ExecutionQueueOptions<T>;

	explicit Channel(const Options &options)
		: mQueue(options)
	{
	}

	bool TrySend(const T &value)
	{
		return mQueue.TryPush(value);
	}

	bool TrySend(T &&value)
	{
		return mQueue.TryPush(std::move(value));
	}

	bool Send(const T &value, int timeoutMs = -1)
	{
		return mQueue.Push(value, timeoutMs);
	}

	bool Send(T &&value, int timeoutMs = -1)
	{
		return mQueue.Push(std::move(value), timeoutMs);
	}

	bool TryReceive(T &out)
	{
		return mQueue.TryPop(out);
	}

	bool Receive(T &out, int timeoutMs = -1)
	{
		return mQueue.Pop(out, timeoutMs);
	}

	void Shutdown()
	{
		mQueue.Shutdown();
	}

	bool IsShutdown() const
	{
		return mQueue.IsShutdown();
	}

	size_t Size() const
	{
		return mQueue.Size();
	}

  private:
	ExecutionQueue<T> mQueue;
};

} // namespace Network::Concurrency
