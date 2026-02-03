#pragma once

#include <cstdint>

namespace Network::Interfaces
{

/**
 * Message type enumeration
 */
enum class MessageType : uint32_t
{
	Unknown = 0,
	Ping = 1,
	Pong = 2,
	CustomStart = 1000
};

} // namespace Network::Interfaces
