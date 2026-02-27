#pragma once

// English: ClientSession - extended session for game clients (replaces GameSession)
// 한글: ClientSession - 게임 클라이언트용 확장 세션 (GameSession 대체)

#include "Network/Core/Session.h"
#include "ClientPacketHandler.h"
#include <memory>
#include <vector>

// Forward declaration
namespace Network::TestServer { class DBTaskQueue; }

#include <memory>

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
        // English: Constructor — inject weak_ptr to DBTaskQueue.
        //          Using weak_ptr instead of a raw pointer prevents use-after-free when
        //          IOCP completion callbacks fire after TestServer begins teardown.
        //          lock() before every use; if the queue is already destroyed, lock() returns
        //          nullptr and the callback safely skips the enqueue.
        // 한글: 생성자 — DBTaskQueue의 weak_ptr 주입.
        //       raw 포인터 대신 weak_ptr을 사용하면 TestServer 소멸 이후에 발생하는
        //       IOCP 완료 콜백에서의 use-after-free를 방지한다.
        //       매 사용 전 lock() 호출; 큐가 이미 소멸되면 nullptr을 반환하고 안전하게 건너뜀.
        explicit ClientSession(std::weak_ptr<DBTaskQueue> dbTaskQueue);
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

        // English: DB task queue — weak_ptr so sessions do not extend the queue's lifetime.
        //          lock() before every access; nullptr means queue is already shut down.
        // 한글: DB 작업 큐 — weak_ptr로 세션이 큐의 수명을 연장하지 않도록 함.
        //       매 접근 전 lock(); nullptr이면 큐가 이미 종료됨.
        std::weak_ptr<DBTaskQueue> mDBTaskQueue;
    };

    using ClientSessionRef = std::shared_ptr<ClientSession>;

} // namespace Network::TestServer
