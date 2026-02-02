#pragma once

// English: TestServer main header - game server using IOCPNetworkEngine
// 한글: TestServer 메인 헤더 - IOCPNetworkEngine 사용 게임 서버

// English: DB support can be disabled if needed
// 한글: 필요시 DB 지원을 비활성화할 수 있음
#ifdef ENABLE_DATABASE_SUPPORT
#include "Database/DBConnectionPool.h"
#endif

#include "Network/Core/IOCPNetworkEngine.h"
#include "Network/Core/Session.h"
#include "Network/Core/SessionManager.h"
#include "Network/Core/PacketDefine.h"
#include "Tests/Protocols/MessageHandler.h"
#include "Tests/Protocols/PingPong.h"
#include "Utils/NetworkUtils.h"
#include <memory>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <mutex>

namespace Network::TestServer
{
    using Utils::ConnectionId;

    // =============================================================================
    // English: GameSession - extended session for game logic
    // 한글: GameSession - 게임 로직용 확장 세션
    // =============================================================================

    class GameSession : public Core::Session
    {
    public:
        GameSession();
        virtual ~GameSession();

        // English: Session event overrides
        // 한글: 세션 이벤트 오버라이드
        void OnConnected() override;
        void OnDisconnected() override;
        void OnRecv(const char* data, uint32_t size) override;

        // English: DB connect time recording
        // 한글: DB 접속 시간 기록
        void RecordConnectTimeToDB();

        bool IsConnectionRecorded() const { return mConnectionRecorded; }

    private:
        // English: Packet handlers
        // 한글: 패킷 핸들러
        void ProcessPacket(const Core::PacketHeader* header, const char* data);
        void HandleConnectRequest(const Core::PKT_SessionConnectReq* packet);
        void HandlePingRequest(const Core::PKT_PingReq* packet);

    private:
        bool mConnectionRecorded;
    };

    using GameSessionRef = std::shared_ptr<GameSession>;

    // =============================================================================
    // English: TestServer class
    // 한글: TestServer 클래스
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

    private:
        // English: Network event handlers
        // 한글: 네트워크 이벤트 핸들러
        void OnConnectionEstablished(const Core::NetworkEventData& eventData);
        void OnConnectionClosed(const Core::NetworkEventData& eventData);
        void OnDataReceived(const Core::NetworkEventData& eventData);

        // English: Session factory
        // 한글: 세션 팩토리
        static Core::SessionRef CreateGameSession();

    private:
        std::unique_ptr<Core::IOCPNetworkEngine>    mEngine;
        std::atomic<bool>                           mIsRunning;
        uint16_t                                    mPort;
        std::string                                 mDbConnectionString;
    };

} // namespace Network::TestServer
