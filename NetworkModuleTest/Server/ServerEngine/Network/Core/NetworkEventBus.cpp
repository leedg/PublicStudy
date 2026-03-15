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

	// English: Collect live channels under shared lock (snapshot), then send outside lock.
	//          Avoids holding the lock during TrySend (which acquires channel-internal locks
	//          and may copy large payloads), preventing starvation of Subscribe/Unsubscribe.
	// 한글: shared 락 내에서 살아있는 채널만 스냅샷 수집 후, 락 해제 뒤 TrySend.
	//       TrySend(채널 내부 락 획득 + 대용량 페이로드 복사) 중 락 보유를 피해
	//       Subscribe/Unsubscribe 스타베이션 방지.
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

	// English: Send outside the lock — channel-internal locking and data copy happen here.
	// 한글: 락 해제 후 전송 — 채널 내부 락 획득 및 data 복사가 여기서 발생.
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
