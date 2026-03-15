#pragma once

// Typed async channel with pluggable queue backend.

#include "ExecutionQueue.h"
#include <utility>

namespace Network::Concurrency
{
// =============================================================================
// Channel<T>
// - Producer/consumer utility that wraps ExecutionQueue<T>.
//
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
