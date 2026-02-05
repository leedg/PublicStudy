#pragma once

// English: TestServer main header - game server using NetworkEngine (multi-platform)
// 한글: TestServer 메인 헤더 - NetworkEngine 사용 게임 서버 (멀티플랫폼)

#include "GameSession.h"
#include "DBServerPacketHandler.h"
#include "DBTaskQueue.h"
#include "Network/Core/NetworkEngine.h"
#include "Network/Core/SessionManager.h"
#include "Utils/NetworkUtils.h"
#include <memory>
#include <atomic>

namespace Network::TestServer
{
    using Utils::ConnectionId;

    // =============================================================================
    // English: TestServer class - manages game clients and DB server connections
    // 한글: TestServer 클래스 - 게임 클라이언트 및 DB 서버 연결 관리
    // =============================================================================

    class TestServer
    {
    public:
        TestServer();
        ~TestServer();

        // English: Lifecycle
        // 한글: 생명주기
        bool Initialize(uint16_t port = 9000, const std::string& dbConnectionString = "");
        bool Start();
        void Stop();
        bool IsRunning() const;

        // English: Connect to DB server
        // 한글: DB 서버에 연결
        bool ConnectToDBServer(const std::string& host, uint16_t port);

    private:
        // English: Network event handlers for client connections
        // 한글: 클라이언트 연결에 대한 네트워크 이벤트 핸들러
        void OnClientConnectionEstablished(const Core::NetworkEventData& eventData);
        void OnClientConnectionClosed(const Core::NetworkEventData& eventData);
        void OnClientDataReceived(const Core::NetworkEventData& eventData);

        // English: Session factory for game clients
        // 한글: 게임 클라이언트용 세션 팩토리
        static Core::SessionRef CreateGameSession();

    private:
        // English: Client connection engine (multi-platform support)
        // 한글: 클라이언트 연결 엔진 (멀티플랫폼 지원)
        std::unique_ptr<Core::INetworkEngine>       mClientEngine;

        // English: DB Server connection
        // 한글: DB 서버 연결
        Core::SessionRef                             mDBServerSession;
        std::unique_ptr<DBServerPacketHandler>      mDBPacketHandler;

        // English: Asynchronous DB task queue (independent of game logic)
        // 한글: 비동기 DB 작업 큐 (게임 로직과 독립적)
        std::unique_ptr<DBTaskQueue>                mDBTaskQueue;

        // English: Server state
        // 한글: 서버 상태
        std::atomic<bool>                           mIsRunning;
        uint16_t                                    mPort;
        std::string                                 mDbConnectionString;
    };

} // namespace Network::TestServer
