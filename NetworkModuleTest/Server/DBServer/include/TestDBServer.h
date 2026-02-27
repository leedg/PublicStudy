#pragma once

// English: TestDBServer main header - database server using NetworkEngine (multi-platform)
// Korean: TestDBServer 메인 헤더 - NetworkEngine 사용 데이터베이스 서버 (멀티플랫폼)

// English: DBPingTimeManager removed — its functionality is now in ServerLatencyManager
// 한글: DBPingTimeManager 제거 — 기능이 ServerLatencyManager에 통합됨
#include "ServerLatencyManager.h"
#include "ServerPacketHandler.h"
#include "OrderedTaskQueue.h"
#include "Network/Core/NetworkEngine.h"
#include "Network/Core/Session.h"
#include "Network/Core/SessionManager.h"
#include "Utils/NetworkUtils.h"
#include <memory>
#include <atomic>

namespace Network::DBServer
{
    using Utils::ConnectionId;

    // =============================================================================
    // English: DBSession - extended session for game server connections
    // Korean: DBSession - 게임 서버 연결용 확장 세션
    // =============================================================================

    class DBSession : public Core::Session
    {
    public:
        DBSession();
        virtual ~DBSession();

        // English: Set packet handler
        // Korean: 패킷 핸들러 설정
        void SetPacketHandler(ServerPacketHandler* handler);

        // English: Session event overrides
        // Korean: 세션 이벤트 오버라이드
        void OnConnected() override;
        void OnDisconnected() override;
        void OnRecv(const char* data, uint32_t size) override;

    private:
        ServerPacketHandler* mPacketHandler;  // Not owned
    };

    using DBSessionRef = std::shared_ptr<DBSession>;

    // =============================================================================
    // English: TestDBServer class - manages database operations for game servers
    // Korean: TestDBServer 클래스 - 게임 서버용 데이터베이스 작업 관리
    // =============================================================================

    class TestDBServer
    {
    public:
        TestDBServer();
        ~TestDBServer();

        // English: Lifecycle
        // Korean: 생명주기
        bool Initialize(uint16_t port = 8001);
        bool Start();
        void Stop();
        bool IsRunning() const;

    private:
        // English: Network event handlers
        // Korean: 네트워크 이벤트 핸들러
        void OnConnectionEstablished(const Core::NetworkEventData& eventData);
        void OnConnectionClosed(const Core::NetworkEventData& eventData);
        void OnDataReceived(const Core::NetworkEventData& eventData);

        // English: Session factory
        // Korean: 세션 팩토리
        Core::SessionRef CreateDBSession();

    private:
        // English: Network engine (multi-platform support)
        // Korean: 네트워크 엔진 (멀티플랫폼 지원)
        std::unique_ptr<Core::INetworkEngine>       mEngine;

        // English: Unified latency manager (handles RTT stats + ping time persistence)
        //          Previously split across ServerLatencyManager + DBPingTimeManager.
        // Korean: 통합 레이턴시 관리자 (RTT 통계 + 핑 시간 저장 모두 담당)
        //         이전에는 ServerLatencyManager + DBPingTimeManager로 분리됐음.
        std::unique_ptr<ServerLatencyManager>       mLatencyManager;

        std::unique_ptr<ServerPacketHandler>        mPacketHandler;

        // English: Ordered task queue for per-serverId ordering guarantee
        //          Uses hash-based thread affinity: same serverId -> same worker thread
        // Korean: serverId별 순서 보장을 위한 순서 보장 작업 큐
        //         해시 기반 스레드 친화도: 같은 serverId -> 같은 워커 스레드
        std::unique_ptr<OrderedTaskQueue>           mOrderedTaskQueue;

        // English: Server state
        // Korean: 서버 상태
        std::atomic<bool>                           mIsRunning;
        uint16_t                                    mPort;
    };

} // namespace Network::DBServer
