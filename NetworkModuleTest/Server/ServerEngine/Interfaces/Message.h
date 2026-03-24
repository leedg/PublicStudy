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
	MessageType          type         = MessageType::Unknown; // 파싱된 메시지 타입
	ConnectionId         connectionId = 0;                    // 송수신 연결 ID
	std::vector<uint8_t> data;                                // 헤더를 제외한 페이로드
	uint64_t             timestamp    = 0;                    // 메시지 생성 시각 (system_clock 밀리초)
};

} // namespace Network::Interfaces
