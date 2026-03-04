// English: NetworkEventBus implementation
// 한글: NetworkEventBus 구현

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

	// English: Upgrade to exclusive lock when pruning dead subscribers.
	//          Use shared lock for the fast/normal path.
	// 한글: 만료된 구독자 제거 시 exclusive 락으로 업그레이드.
	//       일반 경로에서는 shared 락 사용.

	bool needsPrune = false;

	{
		std::shared_lock<std::shared_mutex> readLock(mMutex);
		auto it = mSubscribers.find(key);
		if (it == mSubscribers.end())
		{
			return;
		}

		for (auto &sub : it->second)
		{
			auto channel = sub.channel.lock();
			if (!channel)
			{
				needsPrune = true;
				continue;
			}
			if (!channel->IsShutdown())
			{
				channel->TrySend(data);
			}
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
