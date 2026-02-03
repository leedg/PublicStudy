#pragma once

// English: Server packet handler for TestDBServer
// 한글: TestDBServer용 서버 패킷 핸들러

#include "Network/Core/Session.h"
#include "Network/Core/ServerPacketDefine.h"
#include "DBPingTimeManager.h"
#include "Utils/NetworkUtils.h"
#include <memory>

namespace Network::DBServer
{
    using Utils::ConnectionId;

    // =============================================================================
    // English: ServerPacketHandler - handles packets from game servers
    // 한글: ServerPacketHandler - 게임 서버의 패킷 처리
    // =============================================================================

    class ServerPacketHandler
    {
    public:
        ServerPacketHandler();
        virtual ~ServerPacketHandler();

        // English: Initialize with DB ping time manager
        // 한글: DB ping 시간 관리자로 초기화
        void Initialize(DBPingTimeManager* dbPingTimeManager);

        // English: Process incoming packet from game server
        // 한글: 게임 서버로부터 받은 패킷 처리
        void ProcessPacket(Core::Session* session, const char* data, uint32_t size);

    private:
        // English: Individual packet handlers
        // 한글: 개별 패킷 핸들러들
        void HandleServerPingRequest(Core::Session* session, const Core::PKT_ServerPingReq* packet);
        void HandleDBSavePingTimeRequest(Core::Session* session, const Core::PKT_DBSavePingTimeReq* packet);

    private:
        DBPingTimeManager* mDBPingTimeManager;  // Not owned
    };

} // namespace Network::DBServer
