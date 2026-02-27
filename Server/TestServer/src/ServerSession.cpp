// English: ServerSession implementation
// 한글: ServerSession 구현

#include "../include/ServerSession.h"
#include "Utils/NetworkUtils.h"

namespace Network::TestServer
{
    using namespace Network::Utils;

    ServerSession::ServerSession()
    {
    }

    ServerSession::~ServerSession()
    {
    }

    void ServerSession::OnConnected()
    {
        Logger::Info("ServerSession connected - ID: " + std::to_string(GetId()));
    }

    void ServerSession::OnDisconnected()
    {
        Logger::Info("ServerSession disconnected - ID: " + std::to_string(GetId()));

        if (mReconnectCallback)
        {
            mReconnectCallback();
        }
    }

    void ServerSession::OnRecv(const char* data, uint32_t size)
    {
        (void)data;
        (void)size;
    }

} // namespace Network::TestServer
