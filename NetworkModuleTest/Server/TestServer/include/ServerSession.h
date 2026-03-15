#pragma once

// ServerSession - common base for server-to-server communication sessions

#include "Network/Core/Session.h"
#include <cstdint>
#include <functional>

namespace Network::TestServer
{
    // =============================================================================
    // ServerSession - intermediate base for inter-server sessions
    //          No encryption. Holds ping sequence and connection timestamp.
    //          Not abstract — can be used directly for simple server connections.
    // =============================================================================

    class ServerSession : public Core::Session
    {
    public:
        ServerSession();
        virtual ~ServerSession();

        // Reconnect callback interface — called by owner (e.g. TestServer) on disconnect
        using ReconnectCallback = std::function<void()>;
        void SetReconnectCallback(ReconnectCallback cb) { mReconnectCallback = std::move(cb); }

        // Session event overrides (no-op defaults; subclasses override as needed)
        void OnConnected() override;
        void OnDisconnected() override;
        void OnRecv(const char* data, uint32_t size) override;

    protected:
        ReconnectCallback mReconnectCallback;
    };

    using ServerSessionRef = std::shared_ptr<ServerSession>;

} // namespace Network::TestServer
