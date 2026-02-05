#pragma once

// English: GameSession - extended session for game clients
// 한글: GameSession - 게임 클라이언트용 확장 세션

#include "Network/Core/Session.h"
#include "ClientPacketHandler.h"
#include <memory>

// Forward declaration
namespace Network::TestServer { class DBTaskQueue; }

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

        // English: Set DB task queue for asynchronous DB operations (dependency injection)
        // 한글: 비동기 DB 작업을 위한 DB 작업 큐 설정 (의존성 주입)
        static void SetDBTaskQueue(DBTaskQueue* queue);

        // English: Session event overrides
        // 한글: 세션 이벤트 오버라이드
        void OnConnected() override;
        void OnDisconnected() override;
        void OnRecv(const char* data, uint32_t size) override;

        bool IsConnectionRecorded() const { return mConnectionRecorded; }

    private:
        // English: Asynchronous DB operations (non-blocking)
        // 한글: 비동기 DB 작업 (논블로킹)
        void AsyncRecordConnectTime();
        void AsyncRecordDisconnectTime();

    private:
        bool mConnectionRecorded;
        std::unique_ptr<ClientPacketHandler> mPacketHandler;

        // English: Shared DB task queue (managed by TestServer)
        // 한글: 공유 DB 작업 큐 (TestServer가 관리)
        static DBTaskQueue* sDBTaskQueue;
    };

    using GameSessionRef = std::shared_ptr<GameSession>;

} // namespace Network::TestServer
