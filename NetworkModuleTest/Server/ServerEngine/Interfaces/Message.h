#pragma once

#include "MessageType_enum.h"
#include <cstdint>
#include <vector>

namespace Network::Interfaces
{

// Type definitions
using ConnectionId = uint64_t;

/**
 * Message structure
 */
struct Message
{
	MessageType type = MessageType::Unknown;
	ConnectionId connectionId = 0;
	std::vector<uint8_t> data;
	uint64_t timestamp = 0;
};

} // namespace Network::Interfaces
