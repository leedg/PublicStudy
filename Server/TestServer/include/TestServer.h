#pragma once

// English: TestServer main header - game server using NetworkEngine (multi-platform)
// 한글: TestServer 메인 헤더 - NetworkEngine 사용 게임 서버 (멀티플랫폼)

// English: Forward-declare IDatabase so we can own the local DB without including ServerEngine headers here
// 한글: 여기에 ServerEngine 헤더를 포함하지 않고 로컬 DB를 소유하기 위한 IDatabase 전방 선언
namespace Network { namespace Database { class IDatabase; } }

#include "ClientSession.h"
#include "DBServerSession.h"
#include "DBTaskQueue.h"
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
    // English: TestServer class - manages game clients and DB server connections
    // 한글: TestServer 클래스 - 게임 클라이언트 및 DB 서버 연결 관리
    // =============================================================================

    class TestServer
    {
    public:
        TestServer();
        ~TestServer();

        // English: Lifecycle
        // 한글: 생명주기
        bool Initialize(uint16_t port = 9000, const std::string& dbConnectionString = "");
        bool Start();
        void Stop();
        bool IsRunning() const;

        // English: Connect to DB server
        // 한글: DB 서버에 연결
        bool ConnectToDBServer(const std::string& host, uint16_t port);

    private:
        // English: Network event handlers for client connections
        // 한글: 클라이언트 연결에 대한 네트워크 이벤트 핸들러
        void OnClientConnectionEstablished(const Core::NetworkEventData& eventData);
        void OnClientConnectionClosed(const Core::NetworkEventData& eventData);
        void OnClientDataReceived(const Core::NetworkEventData& eventData);

        // English: Session factory for game clients — returns a lambda capturing
        //          a weak_ptr to mDBTaskQueue (constructor injection, no static global state).
        // 한글: 게임 클라이언트용 세션 팩토리 — mDBTaskQueue의 weak_ptr을 캡처한 람다 반환
        //       (생성자 주입, static 전역 상태 없음).
        Core::SessionFactory MakeClientSessionFactory();

        // English: DB server helpers
        // 한글: DB 서버 연결 헬퍼
        void DisconnectFromDBServer();
        bool SendDBPacket(const void* data, uint32_t size);
        void DBRecvLoop();
        void DBPingLoop();
        void DBReconnectLoop();

    private:
        // English: Client connection engine (multi-platform support)
        // 한글: 클라이언트 연결 엔진 (멀티플랫폼 지원)
        std::unique_ptr<Core::INetworkEngine>       mClientEngine;

        // English: DB Server connection (typed session replaces raw Core::SessionRef)
        // 한글: DB 서버 연결 (raw Core::SessionRef 대신 타입화된 세션 사용)
        DBServerSessionRef                           mDBServerSession;

        // English: Asynchronous DB task queue — shared_ptr so MakeClientSessionFactory can
        //          capture a weak_ptr.  Sessions observe the queue via weak_ptr::lock();
        //          if the queue is destroyed before a late IOCP completion fires, lock()
        //          returns nullptr and the callback skips the enqueue safely (no UAF).
        // 한글: 비동기 DB 작업 큐 — MakeClientSessionFactory에서 weak_ptr 캡처를 위해
        //       shared_ptr 사용. 세션은 weak_ptr::lock()으로 큐에 접근하며,
        //       늦은 IOCP 완료 시 큐가 이미 소멸되면 lock()이 nullptr을 반환하고
        //       콜백이 안전하게 건너뜀 (use-after-free 없음).
        std::shared_ptr<DBTaskQueue>                mDBTaskQueue;

        // English: Local database owned by TestServer, injected into DBTaskQueue.
        //          MockDatabase if dbConnectionString is empty; SQLiteDatabase otherwise.
        //          Outlives mDBTaskQueue (declared first, destroyed last — reversed destruction order).
        // 한글: TestServer가 소유하는 로컬 DB, DBTaskQueue에 주입.
        //       dbConnectionString이 빈 문자열이면 MockDatabase, 아니면 SQLiteDatabase.
        //       mDBTaskQueue보다 오래 살아야 하므로 이 멤버를 먼저 선언(역순 파괴).
        std::unique_ptr<Network::Database::IDatabase> mLocalDatabase;

        // English: Server state
        // 한글: 서버 상태
        std::atomic<bool>                           mIsRunning;
        uint16_t                                    mPort;
        std::string                                 mDbConnectionString;

#ifdef _WIN32
        // English: DB server connection state (Windows-only for now)
        // 한글: DB 서버 연결 상태 (현재 Windows 전용)
        SocketHandle                                mDBServerSocket;
        std::atomic<bool>                           mDBRunning;
        std::atomic<uint32_t>                       mDBPingSequence;
        std::thread                                 mDBRecvThread;
        std::thread                                 mDBPingThread;
        std::mutex                                  mDBSendMutex;
        // English: Condition variable to interrupt DBPingLoop sleep on shutdown
        // 한글: 종료 시 DBPingLoop sleep 즉시 깨우기 위한 조건 변수
        std::condition_variable                     mDBShutdownCV;
        std::mutex                                  mDBShutdownMutex;
        std::vector<char>                           mDBRecvBuffer;
        // English: Read offset for O(1) buffer consumption (avoids O(n) erase)
        // 한글: O(1) 버퍼 소비를 위한 읽기 오프셋 (O(n) erase 방지)
        size_t                                      mDBRecvOffset = 0;
        // English: Stored endpoint for DB reconnect
        // 한글: DB 재연결용 엔드포인트 저장
        std::string                                 mDBHost;
        uint16_t                                    mDBPort = 0;
        std::thread                                 mDBReconnectThread;
        std::atomic<bool>                           mDBReconnectRunning;
        // English: Last WSA error from ConnectToDBServer() — used to distinguish
        //          WSAECONNREFUSED (server shutting down) from other failures
        // 한글: ConnectToDBServer() 실패 시 마지막 WSA 에러 코드
        //       WSAECONNREFUSED(서버 종료 중)와 기타 오류 구분에 사용
        std::atomic<int>                            mLastDBConnectError{0};
#endif
    };

} // namespace Network::TestServer
