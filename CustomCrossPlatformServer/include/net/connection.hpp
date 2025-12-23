// Copyright (c) 2025
//
// Defines the connection abstraction used by the engine. A Connection
// encapsulates the state associated with a single TCP session. This
// header provides a minimal definition; additional functionality
// (e.g. buffering, state machines) can be added later.

#pragma once

#include <cstdint>

namespace net {

class Connection {
public:
    using Id = std::uint64_t;

    explicit Connection(Id id) : id_(id) {}

    /// Retrieve the unique identifier for this connection.
    Id id() const { return id_; }

private:
    Id id_;
    // Future: add state fields such as buffers, protocol state, etc.
};

} // namespace net
