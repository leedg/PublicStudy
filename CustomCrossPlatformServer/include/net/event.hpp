// Copyright (c) 2025
//
// This header defines the event types and structures used by the
// networking engine. These events are produced by platform-specific
// backends and consumed by the core engine and application layers.

#pragma once

#include <cstdint>

namespace net {

/// Enumeration of all possible event types that can be generated
/// by a transport backend. The core engine remains agnostic of
/// the underlying I/O model by using this unified event model.
enum class EventType {
    AcceptCompleted,
    ConnectCompleted,
    RecvCompleted,
    SendCompleted,
    ConnectionClosed,
    ErrorOccurred
};

/// Simple event structure containing information about a completed
/// I/O operation. Backends fill out the fields as appropriate.
struct Event {
    EventType type{};
    /// Identifier for the connection associated with this event. For
    /// listeners, this may be the listener id.
    std::uint64_t connectionId{};
    /// Number of bytes transferred for send/recv operations.
    std::size_t bytesTransferred{};
    /// Platform-specific error code, if any.
    int errorCode{};
};

} // namespace net
