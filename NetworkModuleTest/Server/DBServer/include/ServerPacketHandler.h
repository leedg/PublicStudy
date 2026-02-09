#pragma once

// English: Server packet handler for TestDBServer
// 한글: TestDBServer용 서버 패킷 핸들러

#include "Network/Core/Session.h"
#include "Network/Core/ServerPacketDefine.h"
#include "DBPingTimeManager.h"
#include "ServerLatencyManager.h"
#include "Utils/NetworkUtils.h"
#include <functional>
#include <unordered_map>
#include <memory>

// Forward declaration
namespace Network::DBServer { class OrderedTaskQueue; }

namespace Network::DBServer
{
    using Utils::ConnectionId;

    // =============================================================================
    // English: ServerPacketHandler - handles packets from game servers using functor map
    // 한글: ServerPacketHandler - 펑터 맵을 사용하여 게임 서버 패킷 처리
    // =============================================================================

    class ServerPacketHandler
    {
    public:
        // English: Packet handler functor type
        // 한글: 패킷 핸들러 펑터 타입
        using PacketHandlerFunc = std::function<void(Core::Session*, const char*, uint32_t)>;

        ServerPacketHandler();
        virtual ~ServerPacketHandler();

        // English: Initialize with DB managers and ordered task queue
        // 한글: DB 관리자 및 순서 보장 작업 큐로 초기화
        void Initialize(DBPingTimeManager* dbPingTimeManager,
                        ServerLatencyManager* latencyManager,
                        OrderedTaskQueue* orderedTaskQueue);

        // English: Process incoming packet from game server (uses functor dispatch)
        // 한글: 게임 서버로부터 받은 패킷 처리 (펑터 디스패치 사용)
        void ProcessPacket(Core::Session* session, const char* data, uint32_t size);

    private:
        // English: Packet handler functor map (ServerPacketType -> Handler)
        // 한글: 패킷 핸들러 펑터 맵 (ServerPacketType -> Handler)
        std::unordered_map<uint16_t, PacketHandlerFunc> mHandlers;

        // English: Register all packet handlers
        // 한글: 모든 패킷 핸들러 등록
        void RegisterHandlers();

        // English: Individual packet handlers
        // 한글: 개별 패킷 핸들러들
        void HandleServerPingRequest(Core::Session* session, const Core::PKT_ServerPingReq* packet);
        void HandleDBSavePingTimeRequest(Core::Session* session, const Core::PKT_DBSavePingTimeReq* packet);

    private:
        DBPingTimeManager* mDBPingTimeManager;        // Not owned
        ServerLatencyManager* mLatencyManager;        // Not owned
        OrderedTaskQueue* mOrderedTaskQueue;          // Not owned
    };

} // namespace Network::DBServer
