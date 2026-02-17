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
    // English: ClientSession - handles communication with game clients.
    //
    //   DBTaskQueue ownership: NOT owned here.
    //   The pointer is injected via the constructor (captured in the session factory
    //   lambda inside TestServer::Initialize).  This eliminates the previous static
    //   class variable pattern which acted as a hidden global and prevented multiple
    //   independent TestServer instances from coexisting.
    //
    // 한글: ClientSession - 게임 클라이언트와의 통신 처리.
    //
    //   DBTaskQueue 소유권: 이 클래스가 소유하지 않음.
    //   TestServer::Initialize의 세션 팩토리 람다에서 생성자 주입으로 전달.
    //   이전의 static 클래스 변수(숨겨진 전역 상태)를 제거하여
    //   여러 TestServer 인스턴스가 독립적으로 공존 가능.
    // =============================================================================

    class ClientSession : public Core::Session
    {
    public:
        // English: Constructor — inject DBTaskQueue pointer (not owned, must outlive this session)
        // 한글: 생성자 — DBTaskQueue 포인터 주입 (소유권 없음, 세션보다 오래 살아야 함)
        explicit ClientSession(DBTaskQueue* dbTaskQueue);
        virtual ~ClientSession();

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

        // English: DB task queue — injected via constructor, NOT owned by this class
        // 한글: DB 작업 큐 — 생성자 주입, 이 클래스가 소유하지 않음
        DBTaskQueue* mDBTaskQueue;
    };

    using ClientSessionRef = std::shared_ptr<ClientSession>;

} // namespace Network::TestServer
