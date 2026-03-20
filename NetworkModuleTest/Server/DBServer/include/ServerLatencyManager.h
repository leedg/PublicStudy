#pragma once

// English: Forward-declare IDatabase to avoid pulling in ServerEngine headers
// ?쒓?: ServerEngine ?ㅻ뜑 ?꾩씠 諛⑹?瑜??꾪븳 IDatabase ?꾨갑 ?좎뼵
namespace Network { namespace Database { class IDatabase; } }

// English: ServerLatencyManager - unified per-server latency tracker and ping time recorder.
//
//   Replaces the two separate classes that were previously responsible for these concerns:
//     - ServerLatencyManager  : RTT statistics (min / max / avg) + ServerLatencyLog persistence
//     - DBPingTimeManager     : ping timestamp storage + PingTimeLog persistence  ??MERGED IN
//
//   Both managers wrote to different DB tables but shared identical FormatTimestamp /
//   ExecuteQuery infrastructure, and ServerPacketHandler had to coordinate both in every
//   async task.  Merging eliminates the duplication and halves the dependency list.
//
// ?쒓?: ServerLatencyManager - ?듯빀 ?쒕쾭蹂??덉씠?댁떆 異붿쟻湲?諛????쒓컙 湲곕줉湲?
//
//   湲곗〈????媛쒖쓽 蹂꾨룄 ?대옒?ㅺ? ?섎닠 留〓뜕 ??븷???듯빀:
//     - ServerLatencyManager  : RTT ?듦퀎 (min/max/avg) + ServerLatencyLog ???
//     - DBPingTimeManager     : ping ??꾩뒪?ы봽 ???+ PingTimeLog ??? ???듯빀??
//
//   ??愿由ъ옄???쒕줈 ?ㅻⅨ DB ?뚯씠釉붿뿉 ?곗?留?FormatTimestamp/ExecuteQuery ?명봽?쇰? 怨듭쑀?덇퀬,
//   ServerPacketHandler??留?鍮꾨룞湲??묒뾽留덈떎 ?섏쓣 紐⑤몢 議곗쑉?댁빞 ?덉쓬.
//   ?듯빀?쇰줈 以묐났 ?쒓굅 諛??섏〈???덈컲 媛먯냼.

#include "Utils/NetworkUtils.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Network::DBServer
{
    // =============================================================================
    // English: Per-server latency statistics
    // ?쒓?: ?쒕쾭蹂??덉씠?댁떆 ?듦퀎
    // =============================================================================

    struct ServerLatencyInfo
    {
        uint32_t serverId = 0;
        std::string serverName;

        // English: Latest RTT measurement (ms)
        // ?쒓?: 理쒓렐 RTT 痢≪젙媛?(ms)
        uint64_t lastRttMs = 0;

        // English: Running average RTT (ms)
        // ?쒓?: ?대룞 ?됯퇏 RTT (ms)
        double avgRttMs = 0.0;

        // English: Min/Max RTT (ms)
        // ?쒓?: 理쒖냼/理쒕? RTT (ms)
        uint64_t minRttMs = UINT64_MAX;
        uint64_t maxRttMs = 0;

        // English: Total ping count for this server
        // ?쒓?: ?대떦 ?쒕쾭??珥?Ping ?잛닔
        uint64_t pingCount = 0;

        // English: Timestamp of last measurement
        // ?쒓?: 留덉?留?痢≪젙 ??꾩뒪?ы봽
        uint64_t lastMeasuredTime = 0;
    };

    // =============================================================================
    // English: ServerLatencyManager - per-server latency tracker
    // ?쒓?: ServerLatencyManager - ?쒕쾭蹂??덉씠?댁떆 異붿쟻湲?
    // =============================================================================

    class ServerLatencyManager
    {
    public:
        ServerLatencyManager();
        ~ServerLatencyManager();

        // English: Initialize the manager
        // ?쒓?: 留ㅻ땲? 珥덇린??
        bool Initialize();

        // English: Shutdown the manager
        // ?쒓?: 留ㅻ땲? 醫낅즺
        void Shutdown();

        // ?? RTT statistics ??????????????????????????????????????????????????????

        // English: Record a latency measurement for a server.
        //          Updates in-memory RTT stats and persists to ServerLatencyLog.
        // ?쒓?: ?쒕쾭??????덉씠?댁떆 痢≪젙媛?湲곕줉.
        //       硫붾え由?RTT ?듦퀎 ?낅뜲?댄듃 諛?ServerLatencyLog ???
        // @param serverId    - Server identifier (from PKT_ServerPingReq)
        // @param serverName  - Human-readable server name
        // @param rttMs       - Round-trip time in milliseconds
        // @param timestamp   - Measurement timestamp (ms since epoch, GMT)
        void RecordLatency(uint32_t serverId, const std::string& serverName,
                           uint64_t rttMs, uint64_t timestamp);

        // English: Get latency info for a specific server (thread-safe copy)
        // ?쒓?: ?뱀젙 ?쒕쾭???덉씠?댁떆 ?뺣낫 議고쉶 (?ㅻ젅???덉쟾 蹂듭궗)
        bool GetLatencyInfo(uint32_t serverId, ServerLatencyInfo& outInfo) const;

        // English: Get all server latency infos (thread-safe snapshot)
        // ?쒓?: ?꾩껜 ?쒕쾭 ?덉씠?댁떆 ?뺣낫 議고쉶 (?ㅻ젅???덉쟾 ?ㅻ깄??
        std::unordered_map<uint32_t, ServerLatencyInfo> GetAllLatencyInfos() const;

        // ?? Ping timestamp (merged from DBPingTimeManager) ???????????????????

        // English: Persist a ping timestamp to PingTimeLog for a server.
        //          Previously handled by DBPingTimeManager::SavePingTime.
        // ?쒓?: ?쒕쾭??ping ??꾩뒪?ы봽瑜?PingTimeLog?????
        //       ?댁쟾?먮뒗 DBPingTimeManager::SavePingTime???대떦.
        // @param serverId   - Server identifier
        // @param serverName - Human-readable server name
        // @param timestamp  - Ping timestamp in milliseconds since epoch (GMT)
        // @return true if the write succeeded
        bool SavePingTime(uint32_t serverId, const std::string& serverName,
                          uint64_t timestamp);

        // English: Return the last ping timestamp for a server (in-memory, O(1)).
        //          Returns 0 if the server has never been seen.
        //          Previously handled by DBPingTimeManager::GetLastPingTime.
        // ?쒓?: ?쒕쾭??留덉?留?ping ??꾩뒪?ы봽 諛섑솚 (硫붾え由??? O(1)).
        //       ??踰덈룄 愿痢〓릺吏 ?딆? ?쒕쾭??0 諛섑솚.
        //       ?댁쟾?먮뒗 DBPingTimeManager::GetLastPingTime???대떦.
        uint64_t GetLastPingTime(uint32_t serverId) const;

        bool IsInitialized() const { return mInitialized.load(std::memory_order_acquire); }

        // English: Inject a database connection for persistent storage (non-owning)
        // ?쒓?: ?곸냽 ??μ쓣 ?꾪븳 ?곗씠?곕쿋?댁뒪 二쇱엯 (non-owning)
        void SetDatabase(Network::Database::IDatabase* db);

    private:
        // English: Format millisecond timestamp as "YYYY-MM-DD HH:MM:SS GMT" string
        // ?쒓?: 諛由ъ큹 ??꾩뒪?ы봽瑜?"YYYY-MM-DD HH:MM:SS GMT" 臾몄옄?대줈 ?щ㎎
        std::string FormatTimestamp(uint64_t timestampMs) const;

        // English: Create persistent tables if a live database is available.
        //          Called from both Initialize() and SetDatabase() so that tables are
        //          always created regardless of injection order.
        // ?쒓?: ?쒖꽦 DB媛 ?덉쓣 ???곸냽 ?뚯씠釉??앹꽦.
        //       Initialize()? SetDatabase() ?묒そ?먯꽌 ?몄텧??二쇱엯 ?쒖꽌? 臾닿??섍쾶
        //       ??긽 ?뚯씠釉붿씠 ?앹꽦?섎룄濡??쒕떎.
        void EnsureTables();

    private:
        std::atomic<bool> mInitialized;

        // English: Injected database (non-owning); nullptr = log-only mode
        // ?쒓?: 二쇱엯???곗씠?곕쿋?댁뒪 (non-owning); nullptr?대㈃ 濡쒓렇留?異쒕젰
        Network::Database::IDatabase* mDatabase = nullptr;

        // English: Per-server latency map, guarded by mutex
        // ?쒓?: ?쒕쾭蹂??덉씠?댁떆 留? mutex濡?蹂댄샇
        mutable std::mutex mLatencyMutex;
        std::unordered_map<uint32_t, ServerLatencyInfo> mLatencyMap;

        // English: Last ping timestamp per server (for GetLastPingTime O(1))
        // ?쒓?: ?쒕쾭蹂?留덉?留?Ping ??꾩뒪?ы봽 (GetLastPingTime O(1) 議고쉶??
        mutable std::mutex mPingTimeMutex;
        std::unordered_map<uint32_t, uint64_t> mLastPingTimeMap;
    };

} // namespace Network::DBServer
