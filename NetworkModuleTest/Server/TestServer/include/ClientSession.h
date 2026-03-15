#pragma once

// ClientSession - extended session for game clients (replaces GameSession)

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
    // ClientSession - handles communication with game clients.
    //
    //   DBTaskQueue ownership: NOT owned here.
    //   The pointer is injected via the constructor (captured in the session factory
    //   lambda inside TestServer::Initialize).  This eliminates the previous static
    //   class variable pattern which acted as a hidden global and prevented multiple
    //   independent TestServer instances from coexisting.
    //
    //
    // =============================================================================

    class ClientSession : public Core::Session
    {
    public:
        // Constructor — inject weak_ptr to DBTaskQueue.
        //          Using weak_ptr instead of a raw pointer prevents use-after-free when
        //          IOCP completion callbacks fire after TestServer begins teardown.
        //          lock() before every use; if the queue is already destroyed, lock() returns
        //          nullptr and the callback safely skips the enqueue.
        explicit ClientSession(std::weak_ptr<DBTaskQueue> dbTaskQueue);
        virtual ~ClientSession();

        // Session event overrides
        void OnConnected() override;
        void OnDisconnected() override;
        void OnRecv(const char* data, uint32_t size) override;

        bool IsConnectionRecorded() const { return mConnectionRecorded; }

    protected:
        // Encryption interface (no-op placeholders for future use)
        virtual std::vector<char> Encrypt(const char* data, uint32_t size);
        virtual std::vector<char> Decrypt(const char* data, uint32_t size);

    private:
        // Asynchronous DB operations (non-blocking)
        void AsyncRecordConnectTime();
        void AsyncRecordDisconnectTime();

    private:
        bool mConnectionRecorded;
        std::unique_ptr<ClientPacketHandler> mPacketHandler;

        // DB task queue — weak_ptr so sessions do not extend the queue's lifetime.
        //          lock() before every access; nullptr means queue is already shut down.
        std::weak_ptr<DBTaskQueue> mDBTaskQueue;
    };

    using ClientSessionRef = std::shared_ptr<ClientSession>;

} // namespace Network::TestServer
