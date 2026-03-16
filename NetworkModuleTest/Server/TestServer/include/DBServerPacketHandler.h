#pragma once

// DB Server packet handler for TestServer
// 한글: TestServer용 DB 서버 패킷 핸들러

// Forward declaration to avoid circular includes
// 한글: 순환 include 방지를 위한 전방 선언
namespace Network::TestServer { class DBServerTaskQueue; }

#include "Network/Core/Session.h"
#include "Network/Core/ServerPacketDefine.h"
#include "Utils/NetworkUtils.h"
#include <atomic>
#include <functional>
#include <unordered_map>
#include <memory>

namespace Network::TestServer
{
    using Utils::ConnectionId;

    // =============================================================================
    // DBServerPacketHandler - handles packets from/to DB server using functor array
    // 한글: DBServerPacketHandler - 펑터 배열을 사용하여 DB 서버 패킷 처리
    // =============================================================================

    class DBServerPacketHandler
    {
    public:
        // Packet handler functor type
        // 한글: 패킷 핸들러 펑터 타입
        using PacketHandlerFunc = std::function<void(Core::Session*, const char*, uint32_t)>;

        DBServerPacketHandler();
        virtual ~DBServerPacketHandler();

        // Process incoming packet from DB server (uses functor dispatch)
        // 한글: DB 서버로부터 받은 패킷 처리 (펑터 디스패치 사용)
        void ProcessPacket(Core::Session* session, const char* data, uint32_t size);

        // Send ping to DB server
        // 한글: DB 서버로 Ping 전송
        void SendPingToDBServer(Core::Session* session);

        // Request saving ping time to database
        // 한글: Ping 시간을 데이터베이스에 저장 요청
        void RequestSavePingTime(Core::Session* session, uint32_t serverId, const char* serverName);

        // Inject DBServerTaskQueue for response routing
        // 한글: 응답 라우팅을 위한 DBServerTaskQueue 주입
        void SetTaskQueue(DBServerTaskQueue* queue) { mTaskQueue = queue; }

    private:
        // Packet handler functor map (ServerPacketType -> Handler)
        // 한글: 패킷 핸들러 펑터 맵 (ServerPacketType -> Handler)
        std::unordered_map<uint16_t, PacketHandlerFunc> mHandlers;

        // Register all packet handlers
        // 한글: 모든 패킷 핸들러 등록
        void RegisterHandlers();

        // Individual packet handlers
        // 한글: 개별 패킷 핸들러들
        void HandleServerPongResponse(Core::Session* session, const Core::PKT_ServerPongRes* packet);
        void HandleDBSavePingTimeResponse(Core::Session* session, const Core::PKT_DBSavePingTimeRes* packet);
        void HandleDBQueryResponse(Core::Session* session, const Core::PKT_DBQueryRes* packet);

    private:
        // Ping sequence counter. Atomic because SendPingToDBServer() may be
        //          called from a timer thread while I/O callbacks run on worker threads.
        // 한글: 핑 시퀀스 카운터. 타이머 스레드와 I/O 워커 스레드가 동시에 접근할 수 있으므로 atomic 사용.
        std::atomic<uint32_t> mPingSequence;
        DBServerTaskQueue* mTaskQueue = nullptr;
    };

} // namespace Network::TestServer
