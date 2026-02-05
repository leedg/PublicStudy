// English: GameSession implementation with asynchronous DB operations
// 한글: 비동기 DB 작업을 사용하는 GameSession 구현

#include "../include/GameSession.h"
#include "../include/DBTaskQueue.h"
#include "Utils/NetworkUtils.h"
#include <chrono>
#include <cstring>
#include <ctime>

namespace Network::TestServer
{
    using namespace Network::Core;
    using namespace Network::Utils;

    // English: Static member initialization
    // 한글: 정적 멤버 초기화
    DBTaskQueue* GameSession::sDBTaskQueue = nullptr;

    // =============================================================================
    // English: GameSession implementation
    // 한글: GameSession 구현
    // =============================================================================

    GameSession::GameSession()
        : mConnectionRecorded(false)
        , mPacketHandler(std::make_unique<ClientPacketHandler>())
    {
    }

    GameSession::~GameSession()
    {
    }

    void GameSession::SetDBTaskQueue(DBTaskQueue* queue)
    {
        sDBTaskQueue = queue;
        Logger::Info("GameSession: DBTaskQueue set");
    }

    void GameSession::OnConnected()
    {
        Logger::Info("GameSession connected - ID: " + std::to_string(GetId()));

        // English: Record connect time asynchronously (non-blocking)
        // 한글: 접속 시간을 비동기로 기록 (논블로킹)
        if (!mConnectionRecorded)
        {
            AsyncRecordConnectTime();
            mConnectionRecorded = true;
        }
    }

    void GameSession::OnDisconnected()
    {
        Logger::Info("GameSession disconnected - ID: " + std::to_string(GetId()));

        // English: Record disconnect time asynchronously (non-blocking)
        // 한글: 접속 종료 시간을 비동기로 기록 (논블로킹)
        AsyncRecordDisconnectTime();
    }

    void GameSession::OnRecv(const char* data, uint32_t size)
    {
        if (mPacketHandler)
        {
            mPacketHandler->ProcessPacket(this, data, size);
        }
    }

    void GameSession::AsyncRecordConnectTime()
    {
        // English: Get current time string
        // 한글: 현재 시간 문자열 조회
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

        // English: Submit task to queue (immediate return, processed in background)
        // 한글: 큐에 작업 제출 (즉시 반환, 백그라운드에서 처리)
        if (sDBTaskQueue && sDBTaskQueue->IsRunning())
        {
            sDBTaskQueue->RecordConnectTime(GetId(), timeStr);
            Logger::Debug("Async DB task submitted - RecordConnectTime for Session: " +
                         std::to_string(GetId()));
        }
        else
        {
            Logger::Warn("DBTaskQueue not available - skipping connect time recording for Session: " +
                        std::to_string(GetId()));
        }
    }

    void GameSession::AsyncRecordDisconnectTime()
    {
        // English: Get current time string
        // 한글: 현재 시간 문자열 조회
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

        // English: Submit task to queue (immediate return, processed in background)
        // 한글: 큐에 작업 제출 (즉시 반환, 백그라운드에서 처리)
        if (sDBTaskQueue && sDBTaskQueue->IsRunning())
        {
            sDBTaskQueue->RecordDisconnectTime(GetId(), timeStr);
            Logger::Debug("Async DB task submitted - RecordDisconnectTime for Session: " +
                         std::to_string(GetId()));
        }
        else
        {
            Logger::Warn("DBTaskQueue not available - skipping disconnect time recording for Session: " +
                        std::to_string(GetId()));
        }
    }

} // namespace Network::TestServer
