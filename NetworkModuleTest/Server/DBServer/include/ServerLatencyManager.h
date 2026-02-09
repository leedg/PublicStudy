#pragma once

// English: ServerLatencyManager - tracks per-server latency from Ping/Pong and persists to DB
// 한글: ServerLatencyManager - 서버별 Ping/Pong 레이턴시 측정 및 DB 저장

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

        // English: Record a latency measurement for a server
        // 한글: 서버에 대한 레이턴시 측정값 기록
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

        bool IsInitialized() const { return mInitialized.load(std::memory_order_acquire); }

    private:
        // English: Format latency data as SQL INSERT query
        // 한글: 레이턴시 데이터를 SQL INSERT 쿼리로 포맷
        std::string BuildInsertQuery(uint32_t serverId, const std::string& serverName,
                                     uint64_t rttMs, double avgRttMs,
                                     uint64_t minRttMs, uint64_t maxRttMs,
                                     uint64_t pingCount, uint64_t timestamp);

        // English: Format GMT timestamp as string
        // 한글: GMT 타임스탬프를 문자열로 포맷
        std::string FormatTimestamp(uint64_t timestamp);

        // English: Execute actual database query (placeholder for real implementation)
        // 한글: 실제 데이터베이스 쿼리 실행 (실제 구현을 위한 placeholder)
        bool ExecuteQuery(const std::string& query);

    private:
        std::atomic<bool> mInitialized;

        // English: Per-server latency map, guarded by mutex
        // 한글: 서버별 레이턴시 맵, mutex로 보호
        mutable std::mutex mLatencyMutex;
        std::unordered_map<uint32_t, ServerLatencyInfo> mLatencyMap;
    };

} // namespace Network::DBServer
