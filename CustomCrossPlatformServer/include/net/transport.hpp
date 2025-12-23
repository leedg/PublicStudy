// Copyright (c) 2025
//
// Defines the abstract transport interface for the networking engine.
// Implementations of this interface provide platform-specific I/O
// mechanisms (IOCP, RIO, epoll, io_uring, etc.). The engine and
// application layers interact with network I/O exclusively via this
// interface, enabling runtime switching of backends.

#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <vector>
#include "event.hpp"

namespace net {

class ITransport {
public:
    virtual ~ITransport() = default;

    /// Start the transport backend. This typically spawns any I/O
    /// threads and initializes platform-specific resources.
    virtual void start() = 0;

    /// Stop the transport backend. Implementations must cancel all
    /// outstanding operations and release resources. After stop()
    /// returns, no further events should be delivered.
    virtual void stop() = 0;

    /// Begin listening for incoming connections on the given port.
    /// The backlog parameter controls how many connections can be
    /// queued by the OS before being accepted. This function returns
    /// immediately; connection completions will be delivered via
    /// AcceptCompleted events.
    virtual void listen(std::uint16_t port, int backlog) = 0;

    /// Initiate a connection to a remote host. Returns an opaque
    /// connection identifier that can be used for send/recv operations.
    /// The implementation should deliver a ConnectCompleted event when
    /// the connection attempt completes (success or failure).
    virtual std::uint64_t connect(const char* address, std::uint16_t port) = 0;

    /// Post an asynchronous receive. The caller provides a buffer and
    /// its length. When data arrives, the backend should deliver a
    /// RecvCompleted event containing the number of bytes received.
    virtual void postRecv(std::uint64_t connectionId, void* buffer, std::size_t length) = 0;

    /// Post an asynchronous send. The backend takes ownership of the
    /// buffer for the duration of the send operation. Upon completion,
    /// a SendCompleted event should be delivered.
    virtual void postSend(std::uint64_t connectionId, const void* buffer, std::size_t length) = 0;

    /// Poll for completed events. Implementations may choose to block
    /// for up to timeoutMs milliseconds waiting for events. A timeout
    /// value of 0 should return immediately. An empty vector indicates
    /// that no events are available.
    virtual std::vector<Event> poll(int timeoutMs) = 0;
};

/// Factory function to create a transport backend at runtime based on
/// configuration. Implementations may live in separate translation
/// units depending on the platform and enabled backends.
std::unique_ptr<ITransport> createTransport(const std::string& backendName);

} // namespace net
