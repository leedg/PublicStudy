#pragma once

// English: GameSession - extended session for game clients
// 한글: GameSession - 게임 클라이언트용 확장 세션

#include "Network/Core/Session.h"
#include "ClientPacketHandler.h"
#include <memory>

namespace Network::TestServer
{
    // =============================================================================
    // English: GameSession - handles communication with game clients
    // 한글: GameSession - 게임 클라이언트와의 통신 처리
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
        bool mConnectionRecorded;
        std::unique_ptr<ClientPacketHandler> mPacketHandler;
    };

    using GameSessionRef = std::shared_ptr<GameSession>;

} // namespace Network::TestServer
