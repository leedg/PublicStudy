#pragma once

// 네트워크 메시지 구조체 정의.
// IMessageHandler::ProcessMessage()와 CreateMessage()가 주고받는 공통 타입이다.

#include "MessageType_enum.h"
#include <cstdint>
#include <vector>

namespace Network::Interfaces
{

using ConnectionId = uint64_t;

struct Message
{
	MessageType          type         = MessageType::Unknown;
	ConnectionId         connectionId = 0;
	std::vector<uint8_t> data;        // 헤더를 제외한 페이로드
	uint64_t             timestamp    = 0; // 밀리초 (system_clock 기준)
};

} // namespace Network::Interfaces
