#pragma once

// English: DB Ping Time Manager - handles ping timestamp storage
// 한글: DB Ping 시간 관리자 - ping 타임스탬프 저장 처리

#include <cstdint>
#include <string>
#include <memory>
#include <mutex>
#include <atomic>

namespace Network::DBServer
{
    // =============================================================================
    // English: DBPingTimeManager - manages ping timestamp storage in database
    // 한글: DBPingTimeManager - 데이터베이스에 ping 타임스탬프 저장 관리
    // =============================================================================

    class DBPingTimeManager
    {
    public:
        DBPingTimeManager();
        ~DBPingTimeManager();

        // English: Initialize the manager
        // 한글: 매니저 초기화
        bool Initialize();

        // English: Shutdown the manager
        // 한글: 매니저 종료
        void Shutdown();

        // English: Save ping timestamp to database (GMT)
        // 한글: 데이터베이스에 ping 타임스탬프 저장 (GMT)
        // @param serverId - Server identifier
        // @param serverName - Server name
        // @param timestamp - Ping timestamp in milliseconds since epoch (GMT)
        // @return true if save succeeded, false otherwise
        bool SavePingTime(uint32_t serverId, const std::string& serverName, uint64_t timestamp);

        // English: Get last ping timestamp for a server
        // 한글: 서버의 마지막 ping 타임스탬프 조회
        // @param serverId - Server identifier
        // @return timestamp in milliseconds, or 0 if not found
        uint64_t GetLastPingTime(uint32_t serverId);

        // English: Check if manager is initialized
        // 한글: 매니저 초기화 여부 확인
        bool IsInitialized() const { return mInitialized.load(std::memory_order_acquire); }

    private:
        // English: Internal helper to format GMT timestamp as string
        // 한글: GMT 타임스탬프를 문자열로 포맷하는 내부 헬퍼
        std::string FormatTimestamp(uint64_t timestamp);

        // English: Execute actual database query (placeholder for real implementation)
        // 한글: 실제 데이터베이스 쿼리 실행 (실제 구현을 위한 공백)
        bool ExecuteQuery(const std::string& query);

    private:
        std::atomic<bool> mInitialized;
        std::mutex mMutex;

        // English: Database connection placeholder
        // 한글: 데이터베이스 연결 공백
        // TODO: Add actual database connection here
        // void* mDbConnection;
    };

} // namespace Network::DBServer
