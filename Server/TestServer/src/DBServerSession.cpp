// English: DBServerSession implementation
// 한글: DBServerSession 구현

#include "../include/DBServerSession.h"
#include "Utils/NetworkUtils.h"

namespace Network::TestServer
{
    using namespace Network::Utils;

    DBServerSession::DBServerSession()
        : mPacketHandler(std::make_unique<DBServerPacketHandler>())
    {
    }

    DBServerSession::~DBServerSession()
    {
    }

    void DBServerSession::OnConnected()
    {
        Logger::Info("DBServerSession connected - ID: " + std::to_string(GetId()));
    }

    void DBServerSession::OnDisconnected()
    {
        Logger::Info("DBServerSession disconnected - ID: " + std::to_string(GetId()));

        if (mReconnectCallback)
        {
            mReconnectCallback();
        }
    }

    void DBServerSession::OnRecv(const char* data, uint32_t size)
    {
        if (mPacketHandler)
        {
            mPacketHandler->ProcessPacket(this, data, size);
        }
    }

} // namespace Network::TestServer
