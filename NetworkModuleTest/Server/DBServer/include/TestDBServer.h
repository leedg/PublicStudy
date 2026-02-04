#pragma once

// English: TestDBServer main header - database server using IOCPNetworkEngine
// Korean: TestDBServer 메인 헤더 - IOCPNetworkEngine 사용 데이터베이스 서버

#include "DBPingTimeManager.h"
#include "ServerPacketHandler.h"
#include "Network/Core/IOCPNetworkEngine.h"
#include "Network/Core/Session.h"
#include "Network/Core/SessionManager.h"
#include "Utils/NetworkUtils.h"
#include <memory>
#include <atomic>

namespace Network::DBServer
{
    using Utils::ConnectionId;

    // =============================================================================
    // English: DBSession - extended session for game server connections
    // Korean: DBSession - 게임 서버 연결용 확장 세션
    // =============================================================================

    class DBSession : public Core::Session
    {
    public:
        DBSession();
        virtual ~DBSession();

        // English: Set packet handler
        // Korean: 패킷 핸들러 설정
        void SetPacketHandler(ServerPacketHandler* handler);

        // English: Session event overrides
        // Korean: 세션 이벤트 오버라이드
        void OnConnected() override;
        void OnDisconnected() override;
        void OnRecv(const char* data, uint32_t size) override;

    private:
        ServerPacketHandler* mPacketHandler;  // Not owned
    };

    using DBSessionRef = std::shared_ptr<DBSession>;

    // =============================================================================
    // English: TestDBServer class - manages database operations for game servers
    // Korean: TestDBServer 클래스 - 게임 서버용 데이터베이스 작업 관리
    // =============================================================================

    class TestDBServer
    {
    public:
        TestDBServer();
        ~TestDBServer();

        // English: Lifecycle
        // Korean: 생명주기
        bool Initialize(uint16_t port = 8001);
        bool Start();
        void Stop();
        bool IsRunning() const;

    private:
        // English: Network event handlers
        // Korean: 네트워크 이벤트 핸들러
        void OnConnectionEstablished(const Core::NetworkEventData& eventData);
        void OnConnectionClosed(const Core::NetworkEventData& eventData);
        void OnDataReceived(const Core::NetworkEventData& eventData);

        // English: Session factory
        // Korean: 세션 팩토리
        Core::SessionRef CreateDBSession();

    private:
        std::unique_ptr<Core::IOCPNetworkEngine>    mEngine;
        std::unique_ptr<DBPingTimeManager>          mDBPingTimeManager;
        std::unique_ptr<ServerPacketHandler>        mPacketHandler;

        std::atomic<bool>                           mIsRunning;
        uint16_t                                    mPort;
    };

} // namespace Network::DBServer
