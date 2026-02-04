#pragma once

// English: Common type definitions for Network utilities
// 한글: 네트워크 유틸리티용 공통 타입 정의

#include <cstdint>
#include <cstddef>

namespace Network::Utils
{
// =============================================================================
// English: Type definitions
// 한글: 타입 정의
// =============================================================================

using NetworkHandle = uint64_t;
using ConnectionId = uint64_t;
using MessageId = uint32_t;
using BufferSize = size_t;
using Timestamp = uint64_t;

// =============================================================================
// English: Common constants
// 한글: 공용 상수
// =============================================================================

constexpr uint32_t DEFAULT_PORT = 8000;
constexpr size_t DEFAULT_BUFFER_SIZE = 4096;
constexpr size_t MAX_CONNECTIONS = 10000;
constexpr int DEFAULT_TIMEOUT_MS = 30000;
constexpr Timestamp INVALID_TIMESTAMP = 0;

} // namespace Network::Utils
