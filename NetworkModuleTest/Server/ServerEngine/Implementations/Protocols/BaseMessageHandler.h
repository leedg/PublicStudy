#pragma once

// IMessageHandler의 기본 구현.
// 메시지 파싱/직렬화와 타입별 콜백 디스패치 로직을 제공한다.
// 구체적인 메시지 처리는 이 클래스를 상속하여 구현한다.
//
// 스레드 안전성:
//   RegisterHandler/UnregisterHandler/ProcessMessage 모두 mMutex로 보호된다.

#include "../../Interfaces/IMessageHandler.h"
#include "../../Interfaces/Message.h"
#include "../../Interfaces/MessageType_enum.h"
#include <functional>
#include <mutex>
#include <unordered_map>

namespace Network::Implementations
{

class BaseMessageHandler : public Interfaces::IMessageHandler
{
  public:
	using MessageCallback = std::function<void(const Interfaces::Message &)>;

	BaseMessageHandler();
	virtual ~BaseMessageHandler() = default;

	// IMessageHandler 구현
	bool ProcessMessage(Interfaces::ConnectionId connectionId,
						const uint8_t *data, size_t size) override;

	std::vector<uint8_t> CreateMessage(Interfaces::MessageType type,
										   Interfaces::ConnectionId connectionId,
										   const void *data, size_t size) override;

	uint64_t GetCurrentTimestamp() const override;

	bool ValidateMessage(const uint8_t *data, size_t size) const override;

	/**
	 * 특정 메시지 타입에 대한 콜백을 등록한다.
	 * 같은 타입으로 재등록하면 이전 콜백이 덮어씌워진다.
	 * @param type     등록할 메시지 타입
	 * @param callback 처리 함수
	 * @return callback이 null이면 false
	 */
	bool RegisterHandler(Interfaces::MessageType type,
						 MessageCallback callback);

	/**
	 * 지정 타입의 핸들러 등록을 해제한다.
	 */
	void UnregisterHandler(Interfaces::MessageType type);

  protected:
	/**
	 * 원시 데이터를 Message 구조체로 파싱한다.
	 * 헤더 포맷: [type(4)][connectionId(8)][timestamp(8)][dataSize(4)][data...]
	 */
	virtual bool ParseMessage(Interfaces::ConnectionId connectionId,
								  const uint8_t *data, size_t size,
								  Interfaces::Message &outMessage);

	/**
	 * Message 구조체를 네트워크 전송용 바이트열로 직렬화한다.
	 */
	virtual std::vector<uint8_t>
	SerializeMessage(const Interfaces::Message &message);

	/**
	 * 원시 데이터의 첫 4바이트에서 메시지 타입을 추출한다.
	 */
	static Interfaces::MessageType GetMessageType(const uint8_t *data,
												  size_t size);

  private:
	std::unordered_map<Interfaces::MessageType, MessageCallback> mHandlers; // 타입별 콜백 맵 — mMutex로 보호
	mutable std::mutex                                           mMutex;    // mHandlers 접근 직렬화 (ProcessMessage/Register/Unregister)
	uint32_t                                                     mNextMessageId; // 미래 메시지 추적용 예약 필드 (현재 미사용)
};

} // namespace Network::Implementations
