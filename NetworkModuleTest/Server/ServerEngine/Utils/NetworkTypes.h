#pragma once

// Common type definitions for Network utilities

#include <cstdint>
#include <cstddef>

namespace Network::Utils
{
// =============================================================================
// Type definitions
// =============================================================================

using NetworkHandle = uint64_t;
using ConnectionId = uint64_t;
using MessageId = uint32_t;
using BufferSize = size_t;
using Timestamp = uint64_t;

// =============================================================================
// Common constants
// =============================================================================

constexpr uint32_t DEFAULT_PORT = 8000;
#if defined(_WIN32)
constexpr uint16_t DEFAULT_TEST_SERVER_PORT = 19010;
constexpr uint16_t DEFAULT_TEST_DB_PORT = 18002;
#else
constexpr uint16_t DEFAULT_TEST_SERVER_PORT = 9000;
constexpr uint16_t DEFAULT_TEST_DB_PORT = 8001;
#endif
constexpr size_t DEFAULT_BUFFER_SIZE = 4096;
constexpr size_t MAX_CONNECTIONS = 1000;
constexpr int DEFAULT_TIMEOUT_MS = 30000;
constexpr Timestamp INVALID_TIMESTAMP = 0;

// Soft backpressure threshold for Session::Send().
//          When the per-session send queue reaches this depth, Send() returns
//          SendResult::QueueFull so the caller can react (log, drop, close).
//          Tune this value based on expected burst size and memory budget.
//          Hard limit (MAX_SEND_QUEUE_DEPTH = 1000) is defined in PacketDefine.h.
constexpr size_t SEND_QUEUE_BACKPRESSURE_THRESHOLD = 64;

// Default worker thread counts for DB queues.
//          Override at startup via CLI (-w flag) to match deployment topology.
constexpr size_t DEFAULT_DB_WORKER_COUNT         = 4; // OrderedTaskQueue (DBServer)
constexpr size_t DEFAULT_TASK_QUEUE_WORKER_COUNT = 1; // DBTaskQueue (TestServer) — see DBTaskQueue.h for ordering rationale

} // namespace Network::Utils
