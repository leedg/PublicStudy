#pragma once

// English: DB Server packet handler for TestServer
// 한글: TestServer용 DB 서버 패킷 핸들러

#include "Network/Core/Session.h"
#include "Network/Core/ServerPacketDefine.h"
#include "Utils/NetworkUtils.h"
#include <memory>

namespace Network::TestServer
{
    using Utils::ConnectionId;

    // =============================================================================
    // English: DBServerPacketHandler - handles packets from/to DB server
    // 한글: DBServerPacketHandler - DB 서버와의 패킷 처리
    // =============================================================================

    class DBServerPacketHandler
    {
    public:
        DBServerPacketHandler();
        virtual ~DBServerPacketHandler();

        // English: Process incoming packet from DB server
        // 한글: DB 서버로부터 받은 패킷 처리
        void ProcessPacket(Core::Session* session, const char* data, uint32_t size);

        // English: Send ping to DB server
        // 한글: DB 서버로 Ping 전송
        void SendPingToDBServer(Core::Session* session);

        // English: Request saving ping time to database
        // 한글: Ping 시간을 데이터베이스에 저장 요청
        void RequestSavePingTime(Core::Session* session, uint32_t serverId, const char* serverName);

    private:
        // English: Individual packet handlers
        // 한글: 개별 패킷 핸들러들
        void HandleServerPongResponse(Core::Session* session, const Core::PKT_ServerPongRes* packet);
        void HandleDBSavePingTimeResponse(Core::Session* session, const Core::PKT_DBSavePingTimeRes* packet);

    private:
        uint32_t mPingSequence;  // Ping sequence counter
    };

} // namespace Network::TestServer
