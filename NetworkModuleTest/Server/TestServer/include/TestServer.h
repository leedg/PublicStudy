#pragma once

// English: TestServer main header - game server using IOCPNetworkEngine
// ?쒓?: TestServer 硫붿씤 ?ㅻ뜑 - IOCPNetworkEngine ?ъ슜 寃뚯엫 ?쒕쾭

// English: DB support can be disabled if needed
// ?쒓?: ?꾩슂??DB 吏?먯쓣 鍮꾪솢?깊솕?????덉쓬
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
    // ?쒓?: GameSession - 寃뚯엫 濡쒖쭅???뺤옣 ?몄뀡
    // =============================================================================

    class GameSession : public Core::Session
    {
    public:
        GameSession();
        virtual ~GameSession();

        // English: Session event overrides
        // ?쒓?: ?몄뀡 ?대깽???ㅻ쾭?쇱씠??
        void OnConnected() override;
        void OnDisconnected() override;
        void OnRecv(const char* data, uint32_t size) override;

        // English: DB connect time recording
        // ?쒓?: DB ?묒냽 ?쒓컙 湲곕줉
        void RecordConnectTimeToDB();

        bool IsConnectionRecorded() const { return mConnectionRecorded; }

    private:
        // English: Packet handlers
        // ?쒓?: ?⑦궥 ?몃뱾??
        void ProcessPacket(const Core::PacketHeader* header, const char* data);
        void HandleConnectRequest(const Core::PKT_SessionConnectReq* packet);
        void HandlePingRequest(const Core::PKT_PingReq* packet);

    private:
        bool mConnectionRecorded;
    };

    using GameSessionRef = std::shared_ptr<GameSession>;

    // =============================================================================
    // English: TestServer class
    // ?쒓?: TestServer ?대옒??
    // =============================================================================

    class TestServer
    {
    public:
        TestServer();
        ~TestServer();

        // English: Lifecycle
        // ?쒓?: ?앸챸二쇨린
        bool Initialize(uint16_t port = 9000, const std::string& dbConnectionString = "");
        bool Start();
        void Stop();
        bool IsRunning() const;

    private:
        // English: Network event handlers
        // ?쒓?: ?ㅽ듃?뚰겕 ?대깽???몃뱾??
        void OnConnectionEstablished(const Core::NetworkEventData& eventData);
        void OnConnectionClosed(const Core::NetworkEventData& eventData);
        void OnDataReceived(const Core::NetworkEventData& eventData);

        // English: Session factory
        // ?쒓?: ?몄뀡 ?⑺넗由?
        static Core::SessionRef CreateGameSession();

    private:
        std::unique_ptr<Core::IOCPNetworkEngine>    mEngine;
        std::atomic<bool>                           mIsRunning;
        uint16_t                                    mPort;
        std::string                                 mDbConnectionString;
    };

} // namespace Network::TestServer

