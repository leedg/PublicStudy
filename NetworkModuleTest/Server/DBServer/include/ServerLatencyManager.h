#pragma once

// ServerEngine 헤더 전이 방지를 위한 IDatabase 전방 선언
namespace Network { namespace Database { class IDatabase; } }

// ServerLatencyManager - 서버별 레이턴시 추적 및 핑 시간 기록 통합 관리자.
//
//   이전에는 두 개의 클래스가 각자 역할을 담당했다:
//     - ServerLatencyManager  : RTT 통계 (min/max/avg) + ServerLatencyLog 저장
//     - DBPingTimeManager     : ping 타임스탬프 저장 + PingTimeLog 저장  ← 이 클래스에 병합됨
//
//   두 클래스가 서로 다른 DB 테이블에 쓰면서도 FormatTimestamp/ExecuteQuery
//   인프라를 동일하게 공유했고, ServerPacketHandler가 매 핸들러에서 두 클래스를
//   모두 조율해야 했다. 병합으로 중복을 제거하고 의존성 목록이 절반으로 줄었다.
//
//   메모리 기록(mLatencyMap, mLastPingTimeMap)과 DB 기록을 분리한 이유:
//     - GetLatencyInfo / GetLastPingTime은 I/O 없이 O(1)으로 조회해야 한다.
//     - DB 기록은 실패할 수 있고 느리므로 인메모리 상태와 분리하여
//       DB 오류가 실시간 레이턴시 조회에 영향을 주지 않도록 한다.

#include "Utils/NetworkUtils.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Network::DBServer
{
    // =============================================================================
    // 서버별 레이턴시 통계
    // =============================================================================

    struct ServerLatencyInfo
    {
        uint32_t serverId = 0;           // 서버 식별자 (PKT_ServerPingReq에서 유도)
        std::string serverName;          // 사람이 읽을 수 있는 서버 이름 (로그/DB 저장용)

        // 최신 RTT 측정값 (ms)
        uint64_t lastRttMs = 0;          // 가장 최근 핑 왕복 시간 (ms)

        // 누적 평균 RTT (ms)
        double avgRttMs = 0.0;           // 누적 이동 평균 RTT — pingCount 기반 온라인 갱신

        // 최소/최대 RTT (ms)
        uint64_t minRttMs = UINT64_MAX;  // 관측된 최솟값 (초기값 UINT64_MAX로 첫 측정에서 갱신됨)
        uint64_t maxRttMs = 0;           // 관측된 최댓값

        // 해당 서버의 총 Ping 횟수
        uint64_t pingCount = 0;          // RecordLatency 호출 누적 횟수 (avgRttMs 계산에 사용)

        // 마지막 측정 타임스탬프
        uint64_t lastMeasuredTime = 0;   // 에포크 이후 ms (GMT); 마지막 RecordLatency 호출 시각
    };

    // =============================================================================
    // ServerLatencyManager - 서버별 레이턴시 추적 관리자
    // =============================================================================

    class ServerLatencyManager
    {
    public:
        ServerLatencyManager();
        ~ServerLatencyManager();

        // 관리자 초기화
        bool Initialize();

        // 관리자 종료
        void Shutdown();

        // -- RTT 통계 -----------------------------------------------------------

        // 서버 레이턴시 측정값 기록.
        //   인메모리 RTT 통계를 갱신하고 ServerLatencyLog에 저장한다.
        // @param serverId    - 서버 식별자 (PKT_ServerPingReq에서 유도)
        // @param serverName  - 사람이 읽을 수 있는 서버 이름
        // @param rttMs       - 왕복 시간 (ms)
        // @param timestamp   - 측정 타임스탬프 (에포크 이후 ms, GMT)
        void RecordLatency(uint32_t serverId, const std::string& serverName,
                           uint64_t rttMs, uint64_t timestamp);

        // 특정 서버의 레이턴시 정보 조회 (스레드 안전 복사본)
        bool GetLatencyInfo(uint32_t serverId, ServerLatencyInfo& outInfo) const;

        // 전체 서버 레이턴시 정보 조회 (스레드 안전 스냅샷)
        std::unordered_map<uint32_t, ServerLatencyInfo> GetAllLatencyInfos() const;

        // -- 핑 타임스탬프 (DBPingTimeManager에서 병합) --------------------------

        // 서버의 ping 타임스탬프를 PingTimeLog에 저장.
        //   이전에는 DBPingTimeManager::SavePingTime이 담당.
        // @param serverId   - 서버 식별자
        // @param serverName - 사람이 읽을 수 있는 서버 이름
        // @param timestamp  - Ping 타임스탬프 (에포크 이후 ms, GMT)
        // @return 저장 성공 시 true
        bool SavePingTime(uint32_t serverId, const std::string& serverName,
                          uint64_t timestamp);

        // 서버의 마지막 ping 타임스탬프 반환 (인메모리, O(1)).
        //   서버를 한 번도 보지 못했으면 0 반환.
        //   이전에는 DBPingTimeManager::GetLastPingTime이 담당.
        uint64_t GetLastPingTime(uint32_t serverId) const;

        bool IsInitialized() const { return mInitialized.load(std::memory_order_acquire); }

        // 영구 저장을 위한 데이터베이스 연결 주입 (non-owning)
        void SetDatabase(Network::Database::IDatabase* db);

    private:
        // 밀리초 타임스탬프를 "YYYY-MM-DD HH:MM:SS GMT" 문자열로 포맷
        std::string FormatTimestamp(uint64_t timestampMs) const;

        // 활성 DB가 있으면 영구 테이블을 생성한다.
        //   Initialize()와 SetDatabase() 양쪽에서 호출하여
        //   주입 순서에 관계없이 항상 테이블이 생성되도록 한다.
        void EnsureTables();

    private:
        std::atomic<bool> mInitialized;  // Initialize 후 true, Shutdown 시 false; acquire/release 사용

        // 주입된 데이터베이스 (non-owning); nullptr이면 로그만 출력
        // mDatabase의 모든 읽기/쓰기는 mDatabaseMutex로 보호.
        mutable std::mutex mDatabaseMutex;
        Network::Database::IDatabase* mDatabase = nullptr;  // SetDatabase로 주입; DBServer가 소유 및 수명 관리

        // 서버별 레이턴시 맵 (mutex로 보호)
        mutable std::mutex mLatencyMutex;                        // mLatencyMap 읽기/쓰기 직렬화
        std::unordered_map<uint32_t, ServerLatencyInfo> mLatencyMap;  // serverId → 레이턴시 통계 (인메모리)

        // 서버별 마지막 Ping 타임스탬프 (GetLastPingTime O(1) 조회용)
        mutable std::mutex mPingTimeMutex;                       // mLastPingTimeMap 읽기/쓰기 직렬화
        std::unordered_map<uint32_t, uint64_t> mLastPingTimeMap;  // serverId → 마지막 ping 타임스탬프 (인메모리)
    };

} // namespace Network::DBServer
