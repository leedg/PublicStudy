#pragma once

// English: ClientSession - extended session for game clients (replaces GameSession)
// 한글: ClientSession - 게임 클라이언트용 확장 세션 (GameSession 대체)

#include "Network/Core/Session.h"
#include "ClientPacketHandler.h"
#include <memory>
#include <vector>

// Forward declaration
namespace Network::TestServer { class DBTaskQueue; }

namespace Network::TestServer
{
    // =============================================================================
    // English: ClientSession - handles communication with game clients
    //          Holds encryption interface (currently no-op placeholders)
    // 한글: ClientSession - 게임 클라이언트와의 통신 처리
    //       암호화 인터페이스 보유 (현재는 no-op 플레이스홀더)
    // =============================================================================

    class ClientSession : public Core::Session
    {
    public:
        ClientSession();
        virtual ~ClientSession();

        // English: Set DB task queue for asynchronous DB operations (dependency injection)
        // 한글: 비동기 DB 작업을 위한 DB 작업 큐 설정 (의존성 주입)
        static void SetDBTaskQueue(DBTaskQueue* queue);

        // English: Session event overrides
        // 한글: 세션 이벤트 오버라이드
        void OnConnected() override;
        void OnDisconnected() override;
        void OnRecv(const char* data, uint32_t size) override;

        bool IsConnectionRecorded() const { return mConnectionRecorded; }

    protected:
        // English: Encryption interface (no-op placeholders for future use)
        // 한글: 암호화 인터페이스 (향후 사용을 위한 no-op 플레이스홀더)
        virtual std::vector<char> Encrypt(const char* data, uint32_t size);
        virtual std::vector<char> Decrypt(const char* data, uint32_t size);

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

    using ClientSessionRef = std::shared_ptr<ClientSession>;

} // namespace Network::TestServer
