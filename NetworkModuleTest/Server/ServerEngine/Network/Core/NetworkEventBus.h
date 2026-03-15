#pragma once

// Multi-subscriber event bus for NetworkEvents.
//
// Usage:
//   // Subscribe
//   auto ch = std::make_shared<NetworkEventBus::EventChannel>(
//       Concurrency::ExecutionQueueOptions<NetworkBusEventData>{.mCapacity = 128});
//   auto handle = NetworkEventBus::Instance().Subscribe(NetworkEvent::Connected, ch);
//
//   // Consume (separate thread)
//   NetworkBusEventData evt;
//   while (ch->Receive(evt, 100)) { /* handle */ }
//
//   // Unsubscribe
//   NetworkEventBus::Instance().Unsubscribe(handle);

#include "../../Concurrency/Channel.h"
#include "NetworkEngine.h"
#include <atomic>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace Network::Core
{

// =============================================================================
// Copyable event data for the bus.
//          NetworkEventData owns a unique_ptr and is move-only.
//          NetworkBusEventData uses std::vector<uint8_t> so it is copyable
//          and can be sent to multiple subscriber channels.
// =============================================================================

struct NetworkBusEventData
{
	NetworkEvent eventType{};
	ConnectionId connectionId{0};
	size_t       dataSize{0};
	OSError      errorCode{0};
	Timestamp    timestamp{0};
	std::vector<uint8_t> data; // payload copy (empty if no data)
};

// =============================================================================
// NetworkEventBus — thread-safe singleton event bus.
//          Publish() is called from BaseNetworkEngine::FireEvent().
//          Subscribers own their channels and may drain them from any thread.
// =============================================================================

class NetworkEventBus
{
  public:
	using EventChannel      = Network::Concurrency::Channel<NetworkBusEventData>;
	using SubscriberHandle  = uint64_t;

	static NetworkEventBus &Instance();

	// Publish an event to all channels subscribed to eventType.
	//          Dead weak_ptr subscribers are lazily pruned on the next Publish.
	void Publish(NetworkEvent type, const NetworkBusEventData &data);

	// Subscribe to eventType. Returns a handle for later unsubscription.
	//          channel must remain alive for the subscription to receive events.
	SubscriberHandle Subscribe(NetworkEvent type, std::shared_ptr<EventChannel> channel);

	// Cancel a subscription by handle.
	void Unsubscribe(SubscriberHandle handle);

  private:
	NetworkEventBus() = default;

	struct Subscription
	{
		SubscriberHandle            handle;
		std::weak_ptr<EventChannel> channel;
	};

	// NetworkEvent (uint8_t) → subscriber list.
	std::unordered_map<uint8_t, std::vector<Subscription>> mSubscribers;
	mutable std::shared_mutex                              mMutex;
	std::atomic<SubscriberHandle>                          mNextHandle{1};
};

} // namespace Network::Core
