// ClientSession 구현 — 비동기 DB 작업 위임

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
    // ClientSession 구현
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

        // 접속 시간을 비동기로 기록 (논블로킹)
        if (!mConnectionRecorded)
        {
            AsyncRecordConnectTime();
            mConnectionRecorded = true;
        }
    }

    void ClientSession::OnDisconnected()
    {
        Logger::Info("ClientSession disconnected - ID: " + std::to_string(GetId()));

        // 접속 종료 시간을 비동기로 기록 (논블로킹)
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
        // no-op 플레이스홀더 — 데이터를 그대로 복사
        return std::vector<char>(data, data + size);
    }

    std::vector<char> ClientSession::Decrypt(const char* data, uint32_t size)
    {
        // no-op 플레이스홀더 — 데이터를 그대로 복사
        return std::vector<char>(data, data + size);
    }

    void ClientSession::AsyncRecordConnectTime()
    {
        // 현재 시간 문자열 생성
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

        // 큐에 작업 제출 (즉시 반환, 백그라운드 처리).
        //   weak_ptr::lock() — Stop() 이후 늦은 IOCP 완료 시 nullptr 반환, 안전하게 건너뛴다.
        if (auto queue = mDBTaskQueue.lock())
        {
            // lock() 성공 후 IsRunning() 체크 전에 Shutdown이 시작될 수 있다.
            // 이 경우 이 작업은 손실됨 (graceful shutdown의 의도된 동작).
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
        // 현재 시간 문자열 생성
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

        // AsyncRecordConnectTime과 동일한 이유로 weak_ptr::lock() 사용
        if (auto queue = mDBTaskQueue.lock())
        {
            // lock() 성공 후 IsRunning() 체크 전에 Shutdown이 시작될 수 있다.
            // 이 경우 이 작업은 손실됨 (graceful shutdown의 의도된 동작).
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
