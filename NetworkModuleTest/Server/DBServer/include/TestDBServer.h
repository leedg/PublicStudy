#pragma once

// TestDBServer 메인 헤더 - NetworkEngine 사용 데이터베이스 서버 (멀티플랫폼)
// DBPingTimeManager는 ServerLatencyManager에 통합됨
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
    // DBSession - 게임 서버 연결용 확장 세션
    // =============================================================================

    class DBSession : public Core::Session
    {
    public:
        DBSession();
        virtual ~DBSession();

        // 패킷 핸들러 설정
        void SetPacketHandler(ServerPacketHandler* handler);

        // 세션 이벤트 오버라이드
        void OnConnected() override;
        void OnDisconnected() override;
        void OnRecv(const char* data, uint32_t size) override;

    private:
        ServerPacketHandler* mPacketHandler;  // 수신 패킷 디스패처 (non-owning); TestDBServer가 소유 및 수명 관리
    };

    using DBSessionRef = std::shared_ptr<DBSession>;

    // =============================================================================
    // TestDBServer 클래스 - 게임 서버용 데이터베이스 작업 관리
    // =============================================================================

    class TestDBServer
    {
    public:
        TestDBServer();
        ~TestDBServer();

        // 생명주기
        bool Initialize(uint16_t port           = Utils::DEFAULT_TEST_DB_PORT,
                        size_t   dbWorkerCount = Utils::DEFAULT_DB_WORKER_COUNT);
        bool Start();
        void Stop();
        bool IsRunning() const;

    private:
        // 네트워크 이벤트 핸들러
        void OnConnectionEstablished(const Core::NetworkEventData& eventData);
        void OnConnectionClosed(const Core::NetworkEventData& eventData);
        void OnDataReceived(const Core::NetworkEventData& eventData);


    private:
        // ─────────────────────────────────────────────
        // 네트워크 / 핸들러
        // ─────────────────────────────────────────────

        // 네트워크 엔진 (멀티플랫폼 지원)
        std::unique_ptr<Core::INetworkEngine>       mEngine;  // IOCP/io_uring 등 자동 선택; Stop 시 해제

        // 통합 레이턴시 관리자 (RTT 통계 + 핑 시간 저장 모두 담당).
        //   이전에는 ServerLatencyManager + DBPingTimeManager로 분리됐음.
        std::unique_ptr<ServerLatencyManager>       mLatencyManager;  // mOrderedTaskQueue 람다가 raw 포인터 캡처 — 먼저 선언

        std::unique_ptr<ServerPacketHandler>        mPacketHandler;   // 게임 서버 수신 패킷 디스패처

        // serverId별 순서 보장을 위한 순서 보장 작업 큐.
        // 해시 기반 스레드 친화도: 같은 serverId -> 같은 워커 스레드.
        //
        // *** 선언 순서 중요 ***
        // mOrderedTaskQueue는 반드시 mLatencyManager 이후에 선언해야 한다.
        // C++는 선언 역순으로 소멸시키므로 mOrderedTaskQueue가 먼저 소멸됨.
        // ~OrderedTaskQueue()가 Shutdown()을 호출하여 mLatencyManager raw 포인터를
        // 캡처한 람다들을 모두 드레인한다. 순서가 반전되면 use-after-free 발생.
        std::unique_ptr<OrderedTaskQueue>           mOrderedTaskQueue;  // 소멸 순서 보장: mLatencyManager보다 먼저 소멸

        // ─────────────────────────────────────────────
        // 서버 상태
        // ─────────────────────────────────────────────
        std::atomic<bool>                           mIsRunning;  // Start 후 true, Stop 시 false; load(acquire) 사용
        uint16_t                                    mPort;       // 리슨 포트 (Initialize 시 설정)
    };

} // namespace Network::DBServer
