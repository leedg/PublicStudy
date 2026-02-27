#pragma once

// English: Forward-declare IDatabase to avoid pulling in ServerEngine headers
// 한글: ServerEngine 헤더 전이 방지를 위한 IDatabase 전방 선언
namespace Network { namespace Database { class IDatabase; } }

// English: ServerLatencyManager - unified per-server latency tracker and ping time recorder.
//
//   Replaces the two separate classes that were previously responsible for these concerns:
//     - ServerLatencyManager  : RTT statistics (min / max / avg) + ServerLatencyLog persistence
//     - DBPingTimeManager     : ping timestamp storage + PingTimeLog persistence  ← MERGED IN
//
//   Both managers wrote to different DB tables but shared identical FormatTimestamp /
//   ExecuteQuery infrastructure, and ServerPacketHandler had to coordinate both in every
//   async task.  Merging eliminates the duplication and halves the dependency list.
//
// 한글: ServerLatencyManager - 통합 서버별 레이턴시 추적기 및 핑 시간 기록기.
//
//   기존에 두 개의 별도 클래스가 나눠 맡던 역할을 통합:
//     - ServerLatencyManager  : RTT 통계 (min/max/avg) + ServerLatencyLog 저장
//     - DBPingTimeManager     : ping 타임스탬프 저장 + PingTimeLog 저장  ← 통합됨
//
//   두 관리자는 서로 다른 DB 테이블에 쓰지만 FormatTimestamp/ExecuteQuery 인프라를 공유했고,
//   ServerPacketHandler는 매 비동기 작업마다 둘을 모두 조율해야 했음.
//   통합으로 중복 제거 및 의존성 절반 감소.

#include "Utils/NetworkUtils.h"
#include <cstdint>
#include <string>
#include <mutex>
#include <unordered_map>
#include <atomic>

namespace Network::DBServer
{
    // =============================================================================
    // English: Per-server latency statistics
    // 한글: 서버별 레이턴시 통계
    // =============================================================================

    struct ServerLatencyInfo
    {
        uint32_t serverId = 0;
        std::string serverName;

        // English: Latest RTT measurement (ms)
        // 한글: 최근 RTT 측정값 (ms)
        uint64_t lastRttMs = 0;

        // English: Running average RTT (ms)
        // 한글: 이동 평균 RTT (ms)
        double avgRttMs = 0.0;

        // English: Min/Max RTT (ms)
        // 한글: 최소/최대 RTT (ms)
        uint64_t minRttMs = UINT64_MAX;
        uint64_t maxRttMs = 0;

        // English: Total ping count for this server
        // 한글: 해당 서버의 총 Ping 횟수
        uint64_t pingCount = 0;

        // English: Timestamp of last measurement
        // 한글: 마지막 측정 타임스탬프
        uint64_t lastMeasuredTime = 0;
    };

    // =============================================================================
    // English: ServerLatencyManager - per-server latency tracker
    // 한글: ServerLatencyManager - 서버별 레이턴시 추적기
    // =============================================================================

    class ServerLatencyManager
    {
    public:
        ServerLatencyManager();
        ~ServerLatencyManager();

        // English: Initialize the manager
        // 한글: 매니저 초기화
        bool Initialize();

        // English: Shutdown the manager
        // 한글: 매니저 종료
        void Shutdown();

        // ── RTT statistics ──────────────────────────────────────────────────────

        // English: Record a latency measurement for a server.
        //          Updates in-memory RTT stats and persists to ServerLatencyLog.
        // 한글: 서버에 대한 레이턴시 측정값 기록.
        //       메모리 RTT 통계 업데이트 및 ServerLatencyLog 저장.
        // @param serverId    - Server identifier (from PKT_ServerPingReq)
        // @param serverName  - Human-readable server name
        // @param rttMs       - Round-trip time in milliseconds
        // @param timestamp   - Measurement timestamp (ms since epoch, GMT)
        void RecordLatency(uint32_t serverId, const std::string& serverName,
                           uint64_t rttMs, uint64_t timestamp);

        // English: Get latency info for a specific server (thread-safe copy)
        // 한글: 특정 서버의 레이턴시 정보 조회 (스레드 안전 복사)
        bool GetLatencyInfo(uint32_t serverId, ServerLatencyInfo& outInfo) const;

        // English: Get all server latency infos (thread-safe snapshot)
        // 한글: 전체 서버 레이턴시 정보 조회 (스레드 안전 스냅샷)
        std::unordered_map<uint32_t, ServerLatencyInfo> GetAllLatencyInfos() const;

        // ── Ping timestamp (merged from DBPingTimeManager) ───────────────────

        // English: Persist a ping timestamp to PingTimeLog for a server.
        //          Previously handled by DBPingTimeManager::SavePingTime.
        // 한글: 서버의 ping 타임스탬프를 PingTimeLog에 저장.
        //       이전에는 DBPingTimeManager::SavePingTime이 담당.
        // @param serverId   - Server identifier
        // @param serverName - Human-readable server name
        // @param timestamp  - Ping timestamp in milliseconds since epoch (GMT)
        // @return true if the write succeeded
        bool SavePingTime(uint32_t serverId, const std::string& serverName,
                          uint64_t timestamp);

        // English: Return the last ping timestamp for a server (in-memory, O(1)).
        //          Returns 0 if the server has never been seen.
        //          Previously handled by DBPingTimeManager::GetLastPingTime.
        // 한글: 서버의 마지막 ping 타임스탬프 반환 (메모리 내, O(1)).
        //       한 번도 관측되지 않은 서버는 0 반환.
        //       이전에는 DBPingTimeManager::GetLastPingTime이 담당.
        uint64_t GetLastPingTime(uint32_t serverId) const;

        bool IsInitialized() const { return mInitialized.load(std::memory_order_acquire); }

        // English: Inject a database connection for persistent storage (non-owning)
        // 한글: 영속 저장을 위한 데이터베이스 주입 (non-owning)
        void SetDatabase(Network::Database::IDatabase* db);

    private:
        // English: Escape single quotes in SQL string literals (' → '')
        // 한글: SQL 문자열 리터럴의 single quote 이스케이프 (' → '')
        static std::string EscapeSqlString(const std::string& s);

        // English: Format latency data as SQL INSERT for ServerLatencyLog
        // 한글: ServerLatencyLog용 SQL INSERT 포맷
        std::string BuildLatencyInsertQuery(uint32_t serverId, const std::string& serverName,
                                            uint64_t rttMs, double avgRttMs,
                                            uint64_t minRttMs, uint64_t maxRttMs,
                                            uint64_t pingCount, uint64_t timestamp);

        // English: Format ping data as SQL INSERT for PingTimeLog (merged from DBPingTimeManager)
        // 한글: PingTimeLog용 SQL INSERT 포맷 (DBPingTimeManager에서 통합)
        std::string BuildPingTimeInsertQuery(uint32_t serverId, const std::string& serverName,
                                             uint64_t timestamp);

        // English: Format millisecond timestamp as "YYYY-MM-DD HH:MM:SS GMT" string
        // 한글: 밀리초 타임스탬프를 "YYYY-MM-DD HH:MM:SS GMT" 문자열로 포맷
        std::string FormatTimestamp(uint64_t timestampMs) const;

        // English: Execute a database query
        // 한글: 데이터베이스 쿼리 실행
        bool ExecuteQuery(const std::string& query);

        // English: Create persistent tables if a live database is available.
        //          Called from both Initialize() and SetDatabase() so that tables are
        //          always created regardless of injection order.
        // 한글: 활성 DB가 있을 때 영속 테이블 생성.
        //       Initialize()와 SetDatabase() 양쪽에서 호출해 주입 순서와 무관하게
        //       항상 테이블이 생성되도록 한다.
        void EnsureTables();

    private:
        std::atomic<bool> mInitialized;

        // English: Injected database (non-owning); nullptr = log-only mode
        // 한글: 주입된 데이터베이스 (non-owning); nullptr이면 로그만 출력
        Network::Database::IDatabase* mDatabase = nullptr;

        // English: Per-server latency map, guarded by mutex
        // 한글: 서버별 레이턴시 맵, mutex로 보호
        mutable std::mutex mLatencyMutex;
        std::unordered_map<uint32_t, ServerLatencyInfo> mLatencyMap;

        // English: Last ping timestamp per server (for GetLastPingTime O(1))
        // 한글: 서버별 마지막 Ping 타임스탬프 (GetLastPingTime O(1) 조회용)
        mutable std::mutex mPingTimeMutex;
        std::unordered_map<uint32_t, uint64_t> mLastPingTimeMap;
    };

} // namespace Network::DBServer
