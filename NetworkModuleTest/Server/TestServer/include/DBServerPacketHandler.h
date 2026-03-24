#pragma once

// TestServer용 DB 서버 패킷 핸들러

// 순환 include 방지를 위한 전방 선언
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
    // DBServerPacketHandler - 펑터 맵을 사용하여 DB 서버 패킷 처리
    // =============================================================================

    class DBServerPacketHandler
    {
    public:
        // 패킷 핸들러 펑터 타입
        using PacketHandlerFunc = std::function<void(Core::Session*, const char*, uint32_t)>;

        DBServerPacketHandler();
        virtual ~DBServerPacketHandler();

        // DB 서버로부터 받은 패킷 처리 (펑터 맵 디스패치)
        void ProcessPacket(Core::Session* session, const char* data, uint32_t size);

        // DB 서버로 Ping 전송
        void SendPingToDBServer(Core::Session* session);

        // Ping 시간을 데이터베이스에 저장 요청
        void RequestSavePingTime(Core::Session* session, uint32_t serverId, const char* serverName);

        // DBQueryRes 응답 라우팅을 위한 DBServerTaskQueue 주입
        void SetTaskQueue(DBServerTaskQueue* queue) { mTaskQueue = queue; }

    private:
        // 패킷 핸들러 펑터 맵 (ServerPacketType → Handler)
        std::unordered_map<uint16_t, PacketHandlerFunc> mHandlers;  // 생성자에서 1회 구성 후 read-only (stateless)

        // 생성자에서 모든 패킷 핸들러를 등록
        void RegisterHandlers();

        // 개별 패킷 핸들러
        void HandleServerPongResponse(Core::Session* session, const Core::PKT_ServerPongRes* packet);
        void HandleDBSavePingTimeResponse(Core::Session* session, const Core::PKT_DBSavePingTimeRes* packet);
        void HandleDBQueryResponse(Core::Session* session, const Core::PKT_DBQueryRes* packet);

    private:
        // 핑 시퀀스 카운터.
        //   타이머 스레드(SendPingToDBServer)와 I/O 워커 스레드가 동시에 접근하므로 atomic 사용.
        std::atomic<uint32_t> mPingSequence;   // fetch_add(relaxed) — 단조 증가, 순서 보장 불필요
        DBServerTaskQueue* mTaskQueue = nullptr;  // DBQueryRes 응답 라우팅 대상 (non-owning); SetTaskQueue로 주입
    };

} // namespace Network::TestServer
