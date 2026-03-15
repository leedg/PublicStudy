// NetworkEventBus implementation

#include "NetworkEventBus.h"
#include <algorithm>

namespace Network::Core
{

NetworkEventBus &NetworkEventBus::Instance()
{
	static NetworkEventBus instance;
	return instance;
}

void NetworkEventBus::Publish(NetworkEvent type, const NetworkBusEventData &data)
{
	const auto key = static_cast<uint8_t>(type);

	// Collect live channels under shared lock (snapshot), then send outside lock.
	//          Avoids holding the lock during TrySend (which acquires channel-internal locks
	//          and may copy large payloads), preventing starvation of Subscribe/Unsubscribe.
	std::vector<std::shared_ptr<EventChannel>> liveChannels;
	bool needsPrune = false;

	{
		std::shared_lock<std::shared_mutex> readLock(mMutex);
		auto it = mSubscribers.find(key);
		if (it == mSubscribers.end())
		{
			return;
		}

		liveChannels.reserve(it->second.size());
		for (auto &sub : it->second)
		{
			auto channel = sub.channel.lock();
			if (!channel)
			{
				needsPrune = true;
			}
			else
			{
				liveChannels.push_back(std::move(channel));
			}
		}
	}

	// Send outside the lock — channel-internal locking and data copy happen here.
	for (auto &channel : liveChannels)
	{
		if (!channel->IsShutdown())
		{
			channel->TrySend(data);
		}
	}

	if (needsPrune)
	{
		std::unique_lock<std::shared_mutex> writeLock(mMutex);
		auto it = mSubscribers.find(key);
		if (it != mSubscribers.end())
		{
			auto &subs = it->second;
			subs.erase(
				std::remove_if(subs.begin(), subs.end(),
			                   [](const Subscription &s) { return s.channel.expired(); }),
				subs.end());
		}
	}
}

NetworkEventBus::SubscriberHandle
NetworkEventBus::Subscribe(NetworkEvent type, std::shared_ptr<EventChannel> channel)
{
	const SubscriberHandle handle = mNextHandle.fetch_add(1, std::memory_order_relaxed);

	Subscription sub{handle, channel};
	const auto key = static_cast<uint8_t>(type);

	std::unique_lock<std::shared_mutex> writeLock(mMutex);
	mSubscribers[key].push_back(std::move(sub));

	return handle;
}

void NetworkEventBus::Unsubscribe(SubscriberHandle handle)
{
	std::unique_lock<std::shared_mutex> writeLock(mMutex);

	for (auto &[key, subs] : mSubscribers)
	{
		subs.erase(
			std::remove_if(subs.begin(), subs.end(),
		                   [handle](const Subscription &s) { return s.handle == handle; }),
			subs.end());
	}
}

} // namespace Network::Core
