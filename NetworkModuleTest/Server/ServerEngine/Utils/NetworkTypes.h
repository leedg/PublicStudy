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
constexpr size_t MAX_CONNECTIONS = 1000;
constexpr int DEFAULT_TIMEOUT_MS = 30000;
constexpr Timestamp INVALID_TIMESTAMP = 0;

// English: Soft backpressure threshold for Session::Send().
//          When the per-session send queue reaches this depth, Send() returns
//          SendResult::QueueFull so the caller can react (log, drop, close).
//          Tune this value based on expected burst size and memory budget.
//          Hard limit (MAX_SEND_QUEUE_DEPTH = 1000) is defined in PacketDefine.h.
// 한글: Session::Send()용 소프트 백프레셔 임계값.
//       세션별 송신 큐가 이 깊이에 도달하면 Send()가 SendResult::QueueFull을 반환하여
//       호출자가 로그/드롭/종료 등으로 반응할 수 있도록 함.
//       예상 버스트 크기와 메모리 예산에 따라 조정.
//       하드 한도 (MAX_SEND_QUEUE_DEPTH = 1000)는 PacketDefine.h에 정의.
constexpr size_t SEND_QUEUE_BACKPRESSURE_THRESHOLD = 64;

} // namespace Network::Utils
