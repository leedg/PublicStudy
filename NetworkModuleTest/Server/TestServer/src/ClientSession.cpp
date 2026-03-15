// ClientSession implementation with asynchronous DB operations

#include "../include/ClientSession.h"
#include "../include/DBTaskQueue.h"
#include "Utils/NetworkUtils.h"
#include <chrono>
#include <ctime>

namespace Network::TestServer
{
    using namespace Network::Core;
    using namespace Network::Utils;

    // =============================================================================
    // ClientSession implementation
    // =============================================================================

    ClientSession::ClientSession(std::weak_ptr<DBTaskQueue> dbTaskQueue)
        : mConnectionRecorded(false)
        , mPacketHandler(std::make_unique<ClientPacketHandler>())
        , mDBTaskQueue(std::move(dbTaskQueue))
    {
    }

    ClientSession::~ClientSession()
    {
    }

    void ClientSession::OnConnected()
    {
        Logger::Info("ClientSession connected - ID: " + std::to_string(GetId()));

        // Record connect time asynchronously (non-blocking)
        if (!mConnectionRecorded)
        {
            AsyncRecordConnectTime();
            mConnectionRecorded = true;
        }
    }

    void ClientSession::OnDisconnected()
    {
        Logger::Info("ClientSession disconnected - ID: " + std::to_string(GetId()));

        // Record disconnect time asynchronously (non-blocking)
        AsyncRecordDisconnectTime();
    }

    void ClientSession::OnRecv(const char* data, uint32_t size)
    {
        if (mPacketHandler)
        {
            mPacketHandler->ProcessPacket(this, data, size);
        }
    }

    std::vector<char> ClientSession::Encrypt(const char* data, uint32_t size)
    {
        // No-op placeholder — copy data as-is
        return std::vector<char>(data, data + size);
    }

    std::vector<char> ClientSession::Decrypt(const char* data, uint32_t size)
    {
        // No-op placeholder — copy data as-is
        return std::vector<char>(data, data + size);
    }

    void ClientSession::AsyncRecordConnectTime()
    {
        // Get current time string
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);

        std::tm localTime;
#ifdef _WIN32
        localtime_s(&localTime, &time);
#else
        localtime_r(&time, &localTime);
#endif

        char timeStr[64];
        std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &localTime);

        // Submit task to queue (immediate return, processed in background).
        //          lock() the weak_ptr — if the queue is already destroyed (late IOCP
        //          completion after Stop()), lock() returns nullptr and we skip safely.
        if (auto queue = mDBTaskQueue.lock())
        {
            // Shutdown may begin after lock() succeeds but before IsRunning() check.
            //          If so, this task is lost — intentional behavior for graceful shutdown.
            if (queue->IsRunning())
            {
                queue->RecordConnectTime(GetId(), timeStr);
                Logger::Debug("Async DB task submitted - RecordConnectTime for Session: " +
                             std::to_string(GetId()));
            }
        }
        else
        {
            Logger::Warn("DBTaskQueue not available - skipping connect time recording for Session: " +
                        std::to_string(GetId()));
        }
    }

    void ClientSession::AsyncRecordDisconnectTime()
    {
        // Get current time string
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);

        std::tm localTime;
#ifdef _WIN32
        localtime_s(&localTime, &time);
#else
        localtime_r(&time, &localTime);
#endif

        char timeStr[64];
        std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &localTime);

        // lock() weak_ptr for the same reason as in AsyncRecordConnectTime.
        if (auto queue = mDBTaskQueue.lock())
        {
            // Shutdown may begin after lock() succeeds but before IsRunning() check.
            //          If so, this task is lost — intentional behavior for graceful shutdown.
            if (queue->IsRunning())
            {
                queue->RecordDisconnectTime(GetId(), timeStr);
                Logger::Debug("Async DB task submitted - RecordDisconnectTime for Session: " +
                             std::to_string(GetId()));
            }
        }
        else
        {
            Logger::Warn("DBTaskQueue not available - skipping disconnect time recording for Session: " +
                        std::to_string(GetId()));
        }
    }

} // namespace Network::TestServer
