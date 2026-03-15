#pragma once

// DBServerSession - session for DB server connection

#include "ServerSession.h"
#include "DBServerPacketHandler.h"
#include <memory>

namespace Network::TestServer
{
    // =============================================================================
    // DBServerSession - inherits ServerSession, owns DBServerPacketHandler
    //          OnRecv() delegates to DBServerPacketHandler::ProcessPacket()
    //          Connection lifecycle management is handled by TestServer.
    // =============================================================================

    class DBServerSession : public ServerSession
    {
    public:
        DBServerSession();
        virtual ~DBServerSession();

        // Session event overrides
        void OnConnected() override;
        void OnDisconnected() override;
        void OnRecv(const char* data, uint32_t size) override;

        // Access the packet handler (for sending pings etc.)
        DBServerPacketHandler* GetPacketHandler() { return mPacketHandler.get(); }

    private:
        std::unique_ptr<DBServerPacketHandler> mPacketHandler;
    };

    using DBServerSessionRef = std::shared_ptr<DBServerSession>;

} // namespace Network::TestServer
