#pragma once

// English: Typed async channel with pluggable queue backend.
// 한글: 백엔드 교체가 가능한 타입 기반 비동기 채널.

#include "ExecutionQueue.h"
#include <utility>

namespace Network::Concurrency
{
// =============================================================================
// English: Channel<T>
// - Producer/consumer utility that wraps ExecutionQueue<T>.
//
// 한글: Channel<T>
// - ExecutionQueue<T>를 감싼 생산자/소비자 유틸리티.
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
