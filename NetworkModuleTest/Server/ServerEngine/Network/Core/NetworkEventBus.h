#pragma once

// English: Multi-subscriber event bus for NetworkEvents.
// 한글: NetworkEvent 다중 구독자 이벤트 버스.
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
// English: Copyable event data for the bus.
//          NetworkEventData owns a unique_ptr and is move-only.
//          NetworkBusEventData uses std::vector<uint8_t> so it is copyable
//          and can be sent to multiple subscriber channels.
// 한글: 버스용 복사 가능 이벤트 데이터.
//       NetworkEventData는 unique_ptr 소유로 move-only.
//       NetworkBusEventData는 vector<uint8_t> 사용으로 복사 가능하며
//       다수의 구독자 채널에 전달 가능.
// =============================================================================

struct NetworkBusEventData
{
	NetworkEvent eventType{};
	ConnectionId connectionId{0};
	size_t       dataSize{0};
	OSError      errorCode{0};
	Timestamp    timestamp{0};
	std::vector<uint8_t> data; // English: payload copy (empty if no data) / 한글: 페이로드 복사
};

// =============================================================================
// English: NetworkEventBus — thread-safe singleton event bus.
//          Publish() is called from BaseNetworkEngine::FireEvent().
//          Subscribers own their channels and may drain them from any thread.
// 한글: NetworkEventBus — 스레드 안전 싱글턴 이벤트 버스.
//       Publish()는 BaseNetworkEngine::FireEvent()에서 호출.
//       구독자는 채널을 소유하고 임의 스레드에서 드레인 가능.
// =============================================================================

class NetworkEventBus
{
  public:
	using EventChannel      = Network::Concurrency::Channel<NetworkBusEventData>;
	using SubscriberHandle  = uint64_t;

	static NetworkEventBus &Instance();

	// English: Publish an event to all channels subscribed to eventType.
	//          Dead weak_ptr subscribers are lazily pruned on the next Publish.
	// 한글: eventType을 구독 중인 모든 채널에 이벤트 발행.
	//       만료된 weak_ptr 구독자는 다음 Publish 시 지연 제거.
	void Publish(NetworkEvent type, const NetworkBusEventData &data);

	// English: Subscribe to eventType. Returns a handle for later unsubscription.
	//          channel must remain alive for the subscription to receive events.
	// 한글: eventType 구독. 나중에 구독 해제할 핸들 반환.
	//       구독이 이벤트를 수신하는 동안 channel은 살아있어야 함.
	SubscriberHandle Subscribe(NetworkEvent type, std::shared_ptr<EventChannel> channel);

	// English: Cancel a subscription by handle.
	// 한글: 핸들로 구독 취소.
	void Unsubscribe(SubscriberHandle handle);

  private:
	NetworkEventBus() = default;

	struct Subscription
	{
		SubscriberHandle            handle;
		std::weak_ptr<EventChannel> channel;
	};

	// English: NetworkEvent (uint8_t) → subscriber list.
	// 한글: NetworkEvent(uint8_t) → 구독자 리스트.
	std::unordered_map<uint8_t, std::vector<Subscription>> mSubscribers;
	mutable std::shared_mutex                              mMutex;
	std::atomic<SubscriberHandle>                          mNextHandle{1};
};

} // namespace Network::Core
