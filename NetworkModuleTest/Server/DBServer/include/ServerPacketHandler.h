#pragma once

// TestDBServer용 서버 패킷 핸들러

#include "Network/Core/Session.h"
#include "Network/Core/ServerPacketDefine.h"
#include "ServerLatencyManager.h"   // 통합 레이턴시 + 핑 시간 관리자 (DBPingTimeManager 통합됨)
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
    // ServerPacketHandler - 펑터 맵을 사용하여 게임 서버 패킷 처리.
    //
    //   의존성 (모두 주입됨, 소유 아님):
    //     - ServerLatencyManager  : 통합 RTT 통계 + 핑 시간 저장
    //                               (이전에는 ServerLatencyManager + DBPingTimeManager
    //                                둘 다 필요했으나 DBPingTimeManager가 통합됨)
    //     - OrderedTaskQueue      : serverId별 순서 보장을 위한 해시 기반 스레드 친화도.
    //                               동일 serverId의 레이턴시 기록이 순서대로 DB에 쓰이도록 보장.
    // =============================================================================

    class ServerPacketHandler
    {
    public:
        // 패킷 핸들러 펑터 타입
        using PacketHandlerFunc = std::function<void(Core::Session*, const char*, uint32_t)>;

        ServerPacketHandler();
        virtual ~ServerPacketHandler();

        // 통합 레이턴시 관리자 및 순서 보장 작업 큐로 초기화.
        //   DBPingTimeManager는 더 이상 별도 매개변수가 아님 — ServerLatencyManager에 통합됨.
        void Initialize(ServerLatencyManager* latencyManager,
                        OrderedTaskQueue* orderedTaskQueue);

        // 게임 서버로부터 받은 패킷 처리 (펑터 맵 디스패치)
        void ProcessPacket(Core::Session* session, const char* data, uint32_t size);

    private:
        // 패킷 핸들러 펑터 맵 (ServerPacketType → Handler)
        std::unordered_map<uint16_t, PacketHandlerFunc> mHandlers;  // 생성자에서 1회 구성 후 read-only (stateless)

        // 생성자에서 모든 패킷 핸들러를 등록
        void RegisterHandlers();

        // 개별 패킷 핸들러
        void HandleServerPingRequest(Core::Session* session, const Core::PKT_ServerPingReq* packet);
        void HandleDBSavePingTimeRequest(Core::Session* session, const Core::PKT_DBSavePingTimeReq* packet);

    private:
        ServerLatencyManager* mLatencyManager;        // non-owning — 통합 RTT + 핑 시간 관리자; Initialize로 주입
        OrderedTaskQueue*     mOrderedTaskQueue;      // non-owning — per-serverId 순서 보장 큐; Initialize로 주입
    };

} // namespace Network::DBServer
