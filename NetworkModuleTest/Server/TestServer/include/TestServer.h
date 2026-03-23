#pragma once

// TestServer 메인 헤더 - NetworkEngine 사용 게임 서버 (멀티플랫폼)

// ServerEngine 헤더를 포함하지 않고 로컬 DB를 소유하기 위한 IDatabase 전방 선언
namespace Network { namespace Database { class IDatabase; } }

#include "ClientPacketHandler.h"
#include "DBServerSession.h"
#include "DBServerTaskQueue.h"
#include "DBTaskQueue.h"
#include "Concurrency/TimerQueue.h"
#include "Network/Core/NetworkEngine.h"
#include "Network/Core/SessionManager.h"
#include "Utils/NetworkUtils.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace Network::TestServer
{
    using Utils::ConnectionId;

    // =============================================================================
    // TestServer 클래스 - 게임 클라이언트 연결과 DB 서버 연결을 함께 관리한다.
    // =============================================================================

    class TestServer
    {
    public:
        TestServer();
        ~TestServer();

        // 생명주기
        bool Initialize(uint16_t port                  = Utils::DEFAULT_TEST_SERVER_PORT,
                        const std::string& dbConnectionString = "",
                        const std::string& engineType         = "auto",
                        size_t             dbWorkerCount      = Utils::DEFAULT_TASK_QUEUE_WORKER_COUNT);
        bool Start();
        void Stop();
        bool IsRunning() const;

        // DB 서버 연결
        bool ConnectToDBServer(const std::string& host, uint16_t port);

        // DBServerTaskQueue 셀프 테스트 (check-failure 경로, 네트워크 불필요).
        // 모든 assertion이 통과하면 true 반환.
        bool RunSelfTest();

    private:
        // 클라이언트 연결 네트워크 이벤트 핸들러.
        // OnClientConnectionEstablished/Closed에서 직접 DB를 접근하지 않는다.
        //   이유: 이벤트 핸들러는 IOCP 완료 스레드에서 호출되므로 블로킹 DB 호출이
        //   불가하다. 대신 DBTaskQueue에 비동기 작업으로 위임한다.
        void OnClientConnectionEstablished(const Core::NetworkEventData& eventData);
        void OnClientConnectionClosed(const Core::NetworkEventData& eventData);
        void OnClientDataReceived(const Core::NetworkEventData& eventData);

        // DB 서버 연결 헬퍼
        void DisconnectFromDBServer();
        bool SendDBPacket(const void* data, uint32_t size);
        void DBRecvLoop();
        void SendDBPing();   // DB 핑 1회 전송 (타이머 콜백)
        void DBReconnectLoop();

    private:
        // 모든 클라이언트 세션에서 공유하는 패킷 핸들러 (생성 후 stateless).
        // TestServer 생성자에서 1회 할당, 세션별 OnRecv 콜백에 주입.
        std::unique_ptr<ClientPacketHandler>        mPacketHandler;

        // 클라이언트 연결 엔진 (멀티플랫폼 지원)
        std::unique_ptr<Core::INetworkEngine>       mClientEngine;

        // DB 서버 연결 세션 (raw Core::SessionRef 대신 타입화된 세션 사용)
        DBServerSessionRef                           mDBServerSession;

        // TestServer가 소유하는 로컬 DB, DBTaskQueue에 주입.
        //   dbConnectionString이 비면 MockDatabase, 아니면 SQLiteDatabase.
        //   mDBTaskQueue보다 오래 살아야 하므로 먼저 선언 (C++ 역순 소멸 보장).
        std::unique_ptr<Network::Database::IDatabase> mLocalDatabase;

        // 비동기 DB 작업 큐 — shared_ptr로 유지하여 세션 팩토리 람다가 weak_ptr을 캡처한다.
        //   세션 OnRecv 콜백은 weak_ptr::lock()으로 큐에 접근하며,
        //   TestServer 종료 후 늦게 도착한 IOCP 완료에서 큐가 이미 소멸됐을 경우
        //   lock()이 nullptr을 반환하므로 use-after-free 없이 안전하게 건너뛴다.
        std::shared_ptr<DBTaskQueue>                mDBTaskQueue;

        // 클라이언트 요청을 TestDBServer로 중계하는 비동기 작업 큐
        std::shared_ptr<DBServerTaskQueue>          mDBServerTaskQueue;

        // 서버 상태
        std::atomic<bool>                           mIsRunning;
        uint16_t                                    mPort;
        std::string                                 mDbConnectionString;
        std::string                                 mEngineType;

#ifdef _WIN32
        // DB 서버 연결 상태 (현재 Windows 전용)
        SocketHandle                                mDBServerSocket;
        std::atomic<bool>                           mDBRunning;
        std::atomic<uint32_t>                       mDBPingSequence;
        std::thread                                 mDBRecvThread;
        // 이전의 mDBPingThread를 TimerQueue로 교체 — mDBPingTimer가 핸들 보유.
        Network::Concurrency::TimerQueue            mTimerQueue;
        Network::Concurrency::TimerQueue::TimerHandle mDBPingTimer{0};
        std::mutex                                  mDBSendMutex;
        // 종료 시 재연결 루프 backoff sleep을 즉시 깨우기 위한 조건 변수.
        // Stop()이 notify_all()로 DBReconnectLoop를 중단시킨다.
        std::condition_variable                     mDBShutdownCV;
        std::mutex                                  mDBShutdownMutex;
        std::vector<char>                           mDBRecvBuffer;
        // 읽기 오프셋 — O(1) 버퍼 소비를 위해 사용 (O(n) erase 방지)
        size_t                                      mDBRecvOffset = 0;
        // DB 재연결 시 사용할 엔드포인트 저장
        std::string                                 mDBHost;
        uint16_t                                    mDBPort = 0;
        std::thread                                 mDBReconnectThread;
        std::atomic<bool>                           mDBReconnectRunning;
        // ConnectToDBServer() 실패 시 마지막 WSA 에러 코드.
        // WSAECONNREFUSED(10061, 서버 종료/기동 중)와 기타 오류를 구분하여
        // DBReconnectLoop에서 백오프 전략을 결정한다.
        std::atomic<int>                            mLastDBConnectError{0};
#endif
    };

} // namespace Network::TestServer
