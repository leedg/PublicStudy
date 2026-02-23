// English: TestServer implementation with separated handlers
// Korean: 분리된 핸들러를 가진 TestServer 구현

#include "../include/TestServer.h"
#include "Network/Core/ServerPacketDefine.h"
// English: Full IDatabase + DatabaseFactory definitions needed to create the local database instance
// 한글: 로컬 DB 인스턴스 생성에 필요한 IDatabase / DatabaseFactory 전체 정의
#include "Interfaces/IDatabase.h"
#include "Database/DatabaseFactory.h"
#include <iostream>
#include <mutex>
#include <thread>
#include <chrono>
#include <cstring>
#include <ctime>

#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#endif

namespace Network::TestServer
{
    using namespace Network::Core;
    using namespace Network::Utils;

    // =============================================================================
    // English: TestServer implementation
    // Korean: TestServer 구현
    // =============================================================================

    TestServer::TestServer()
        : mIsRunning(false)
        , mPort(0)
#ifdef _WIN32
        , mDBServerSocket(INVALID_SOCKET)
        , mDBRunning(false)
        , mDBPingSequence(0)
        , mDBReconnectRunning(false)
#endif
    {
#ifdef _WIN32
        mDBRecvBuffer.reserve(8192 * 4);
#endif
    }

    TestServer::~TestServer()
    {
        if (mIsRunning.load())
        {
            Stop();
        }
    }

    bool TestServer::Initialize(uint16_t port, const std::string& dbConnectionString)
    {
        mPort = port;
        mDbConnectionString = dbConnectionString;

        // English: Initialize asynchronous DB task queue FIRST (needed by session factory).
        //          1 worker thread is intentional: guarantees per-session task ordering.
        //          RecordConnectTime / RecordDisconnectTime for the same sessionId must
        //          execute in submission order. With 2+ workers, tasks for the same session
        //          can run on different threads simultaneously — breaking ordering.
        //          Use OrderedTaskQueue (hash-based affinity) if multi-worker throughput
        //          is required while preserving per-session ordering.
        // 한글: 비동기 DB 작업 큐를 먼저 초기화 (세션 팩토리에서 필요).
        //       워커 스레드 1개는 의도적: 세션별 작업 순서를 보장하기 위함.
        //       같은 sessionId의 RecordConnectTime / RecordDisconnectTime은 제출 순서대로
        //       실행되어야 함. 2개 이상 워커 시 동일 세션 작업이 서로 다른 스레드에서
        //       동시 실행될 수 있어 순서가 깨짐.
        //       멀티워커 처리량이 필요하면 OrderedTaskQueue(해시 기반 친화도)로 전환.
        // English: Create local database — SQLite when a path is given, Mock otherwise.
        //          The owned instance is injected into DBTaskQueue so it can persist
        //          connect/disconnect/player-data records without a separate DB server.
        // 한글: 로컬 DB 생성 — 경로가 있으면 SQLite, 없으면 Mock.
        //       소유 인스턴스를 DBTaskQueue에 주입하여 접속/해제/플레이어 데이터를
        //       별도 DB 서버 없이 저장할 수 있도록 한다.
        {
            using namespace Network::Database;
            if (mDbConnectionString.empty())
            {
                mLocalDatabase = DatabaseFactory::CreateMockDatabase();
                DatabaseConfig cfg;
                cfg.mType = DatabaseType::Mock;
                mLocalDatabase->Connect(cfg);
                Logger::Info("TestServer: using MockDatabase for DBTaskQueue");
            }
            else
            {
                mLocalDatabase = DatabaseFactory::CreateSQLiteDatabase();
                DatabaseConfig cfg;
                cfg.mType = DatabaseType::SQLite;
                cfg.mConnectionString = mDbConnectionString;
                mLocalDatabase->Connect(cfg);
                Logger::Info("TestServer: using SQLiteDatabase (" + mDbConnectionString + ") for DBTaskQueue");
            }
        }

        mDBTaskQueue = std::make_shared<DBTaskQueue>();
        if (!mDBTaskQueue->Initialize(1, "db_tasks.wal", mLocalDatabase.get()))
        {
            Logger::Error("Failed to initialize DB task queue");
            return false;
        }

        // English: Set Session factory. MakeClientSessionFactory() captures a weak_ptr
        //          to mDBTaskQueue for constructor injection, replacing the previous
        //          static class variable pattern (hidden global state).
        // 한글: 세션 팩토리 설정. MakeClientSessionFactory()가 mDBTaskQueue의 weak_ptr을
        //       캡처해 생성자 주입을 수행하며, 이전 static 클래스 변수(숨겨진 전역 상태)를 대체.
        Core::SessionManager::Instance().Initialize(MakeClientSessionFactory());

        // English: Create and initialize client network engine using factory (auto-detect best backend)
        // Korean: 팩토리를 사용하여 클라이언트 네트워크 엔진 생성 및 초기화 (최적 백엔드 자동 감지)
        mClientEngine = CreateNetworkEngine("auto");
        if (!mClientEngine)
        {
            Logger::Error("Failed to create network engine");
            return false;
        }

        constexpr size_t MAX_CONNECTIONS = 10000;
        if (!mClientEngine->Initialize(MAX_CONNECTIONS, port))
        {
            Logger::Error("Failed to initialize client network engine");
            return false;
        }

        // English: Register event callbacks for client connections
        // Korean: 클라이언트 연결에 대한 이벤트 콜백 등록
        mClientEngine->RegisterEventCallback(NetworkEvent::Connected,
            [this](const NetworkEventData& e) { OnClientConnectionEstablished(e); });

        mClientEngine->RegisterEventCallback(NetworkEvent::Disconnected,
            [this](const NetworkEventData& e) { OnClientConnectionClosed(e); });

        mClientEngine->RegisterEventCallback(NetworkEvent::DataReceived,
            [this](const NetworkEventData& e) { OnClientDataReceived(e); });

        Logger::Info("TestServer initialized on port " + std::to_string(port));
        return true;
    }

    bool TestServer::Start()
    {
        if (!mClientEngine)
        {
            Logger::Error("TestServer not initialized");
            return false;
        }

        if (!mClientEngine->Start())
        {
            Logger::Error("Failed to start client network engine");
            return false;
        }

        mIsRunning.store(true);
        Logger::Info("TestServer started");
        return true;
    }

    void TestServer::Stop()
    {
        if (!mIsRunning.load())
        {
            return;
        }

        mIsRunning.store(false);

        // English: Wake reconnect loop immediately (if waiting in backoff sleep)
        // 한글: 재연결 루프가 백오프 대기 중이면 즉시 깨움
        mDBShutdownCV.notify_all();

        if (mDBReconnectThread.joinable())
        {
            mDBReconnectThread.join();
        }

        // English: Record disconnect events for still-connected sessions before
        //          shutting down DBTaskQueue so Stop() does not lose terminal records.
        // 한글: Stop() 중 마지막 disconnect 기록이 누락되지 않도록 DBTaskQueue 종료 전에
        //       아직 연결된 세션들의 종료 기록을 먼저 큐잉.
        if (mDBTaskQueue && mDBTaskQueue->IsRunning())
        {
            std::tm localTime{};
            std::time_t rawNow = std::time(nullptr);
#ifdef _WIN32
            localtime_s(&localTime, &rawNow);
#else
            localtime_r(&rawNow, &localTime);
#endif

            char timeStr[64] = {0};
            std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &localTime);
            const std::string shutdownTime(timeStr);

            size_t queuedDisconnectCount = 0;
            Core::SessionManager::Instance().ForEachSession(
                [this, &shutdownTime, &queuedDisconnectCount](Core::SessionRef session)
                {
                    if (!session || !session->IsConnected())
                    {
                        return;
                    }

                    mDBTaskQueue->RecordDisconnectTime(session->GetId(), shutdownTime);
                    ++queuedDisconnectCount;
                });

            if (queuedDisconnectCount > 0)
            {
                Logger::Info("Queued disconnect records during shutdown: " +
                             std::to_string(queuedDisconnectCount));
            }
        }

        // English: Step 1 - Flush DB task queue (complete pending tasks while DB connection is still alive)
        // Korean: 1단계 - DB 태스크 큐 드레인 (DB 연결이 살아있는 동안 대기 중인 작업 완료)
        if (mDBTaskQueue)
        {
            Logger::Info("Shutting down DB task queue...");
            mDBTaskQueue->Shutdown();
            Logger::Info("DB task queue statistics - Processed: " +
                        std::to_string(mDBTaskQueue->GetProcessedCount()) +
                        ", Failed: " + std::to_string(mDBTaskQueue->GetFailedCount()));
        }

        // English: Disconnect local database after the queue is fully drained.
        // 한글: 큐가 완전히 드레인된 후 로컬 DB 연결 해제.
        if (mLocalDatabase)
        {
            mLocalDatabase->Disconnect();
            mLocalDatabase.reset();
            Logger::Info("TestServer: local database disconnected");
        }

        // English: Step 2 - Disconnect from DB server BEFORE mClientEngine->Stop()
        //          mClientEngine->Stop() calls WSACleanup() which invalidates mDBServerSocket.
        //          Closing DB socket after WSACleanup causes WSAECONNRESET(10054) in DBRecvLoop.
        // Korean: 2단계 - mClientEngine->Stop() 전에 DB 서버 연결 해제
        //         mClientEngine->Stop()은 WSACleanup()을 호출해 mDBServerSocket을 무효화함.
        //         WSACleanup 후 DB 소켓 종료 시 DBRecvLoop에서 WSAECONNRESET(10054) 발생.
        DisconnectFromDBServer();

        // English: Step 3 - Stop client network engine (closes IOCP/RIO, calls WSACleanup)
        // Korean: 3단계 - 클라이언트 네트워크 엔진 종료 (IOCP/RIO 종료, WSACleanup 호출)
        if (mClientEngine)
        {
            mClientEngine->Stop();
        }

        Logger::Info("TestServer stopped");
    }

    bool TestServer::IsRunning() const
    {
        return mIsRunning.load();
    }

    bool TestServer::ConnectToDBServer(const std::string& host, uint16_t port)
    {
        Logger::Info("Connecting to DB server at " + host + ":" + std::to_string(port));

#ifdef _WIN32
        // English: Store endpoint for reconnect loop
        // 한글: 재연결 루프용 엔드포인트 저장
        mDBHost = host;
        mDBPort = port;

        if (mDBRunning.load())
        {
            Logger::Warn("DB server connection already running");
            return true;
        }

        // English: Join previous recv/ping threads before reusing (reconnect path)
        // 한글: 재연결 경로에서 재사용 전 이전 스레드 join
        if (mDBRecvThread.joinable()) mDBRecvThread.join();
        if (mDBPingThread.joinable()) mDBPingThread.join();

        // English: Initialize Winsock exactly once (thread-safe via call_once)
        // 한글: Winsock을 정확히 한 번 초기화 (call_once로 스레드 안전)
        WSADATA wsaData;
        static std::once_flag sWsaInitFlag;
        bool wsaOk = true;
        std::call_once(sWsaInitFlag, [&]() {
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
            {
                Logger::Error("WSAStartup failed");
                wsaOk = false;
            }
        });
        if (!wsaOk) return false;

        // English: Create client socket
        // 한글: 클라이언트 소켓 생성
        SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (clientSocket == INVALID_SOCKET)
        {
            mLastDBConnectError.store(WSAGetLastError());
            Logger::Error("Failed to create client socket: " + std::to_string(mLastDBConnectError.load()));
            return false;
        }

        // English: Set up server address
        // 한글: 서버 주소 설정
        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);

        // English: Convert host string to address
        // 한글: 호스트 문자열을 주소로 변환
        if (inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr) <= 0)
        {
            Logger::Error("Invalid address: " + host);
            closesocket(clientSocket);
            return false;
        }

        // English: Connect to DB server
        // 한글: DB 서버에 연결
        if (connect(clientSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR)
        {
            mLastDBConnectError.store(WSAGetLastError());
            Logger::Error("Failed to connect to DB server: " + std::to_string(mLastDBConnectError.load()));
            closesocket(clientSocket);
            return false;
        }

        // English: Reset last error on success
        // 한글: 성공 시 마지막 에러 초기화
        mLastDBConnectError.store(0);

        // English: Create and initialize DBServerSession for DB connection
        // 한글: DB 연결을 위한 DBServerSession 생성 및 초기화
        mDBServerSession = std::make_shared<DBServerSession>();
        mDBServerSession->Initialize(static_cast<uint64_t>(clientSocket), clientSocket);

        mDBServerSocket = clientSocket;
        mDBRunning.store(true);

        // English: Start DB recv/ping threads
        // 한글: DB 수신/핑 스레드 시작
        mDBRecvThread = std::thread(&TestServer::DBRecvLoop, this);
        mDBPingThread = std::thread(&TestServer::DBPingLoop, this);

        Logger::Info("Successfully connected to DB server at " + host + ":" + std::to_string(port));
        return true;
#else
        // English: Non-Windows platforms not yet supported for client connections
        // 한글: Windows가 아닌 플랫폼에서는 아직 클라이언트 연결을 지원하지 않음
        Logger::Error("Client connection not implemented for non-Windows platforms");
        return false;
#endif
    }

    void TestServer::OnClientConnectionEstablished(const NetworkEventData& eventData)
    {
        Logger::Info("Client connected - Connection: " + std::to_string(eventData.connectionId));
    }

    void TestServer::OnClientConnectionClosed(const NetworkEventData& eventData)
    {
        Logger::Info("Client disconnected - Connection: " + std::to_string(eventData.connectionId));
    }

    void TestServer::OnClientDataReceived(const NetworkEventData& eventData)
    {
        Logger::Debug("Received " + std::to_string(eventData.dataSize) +
            " bytes from client Connection: " + std::to_string(eventData.connectionId));
    }

    Core::SessionFactory TestServer::MakeClientSessionFactory()
    {
        // English: Capture a weak_ptr — if mDBTaskQueue is destroyed before a late IOCP
        //          completion fires, weak_ptr::lock() in ClientSession returns nullptr and
        //          the callback skips the enqueue safely (prevents use-after-free).
        // 한글: weak_ptr 캡처 — 늦은 IOCP 완료 이전에 mDBTaskQueue가 소멸되어도
        //       ClientSession::weak_ptr::lock()이 nullptr을 반환하고 안전하게 건너뜀
        //       (use-after-free 방지).
        std::weak_ptr<DBTaskQueue> weakQueue = mDBTaskQueue;
        return [weakQueue]() -> Core::SessionRef
        {
            return std::make_shared<ClientSession>(weakQueue);
        };
    }

    void TestServer::DisconnectFromDBServer()
    {
#ifdef _WIN32
        if (!mDBRunning.load())
        {
            return;
        }

        mDBRunning.store(false);

        // English: Wake up DBPingLoop immediately (avoids up to 5s sleep wait)
        // 한글: DBPingLoop 즉시 깨우기 (최대 5초 sleep 대기 방지)
        mDBShutdownCV.notify_all();

        if (mDBServerSocket != INVALID_SOCKET)
        {
            shutdown(mDBServerSocket, SD_BOTH);
        }

        if (mDBRecvThread.joinable())
        {
            mDBRecvThread.join();
        }

        if (mDBPingThread.joinable())
        {
            mDBPingThread.join();
        }

        if (mDBServerSession)
        {
            mDBServerSession->Close();
            mDBServerSession.reset();
        }

        mDBServerSocket = INVALID_SOCKET;
        mDBRecvBuffer.clear();
        mDBRecvOffset = 0;
#endif
    }

    bool TestServer::SendDBPacket(const void* data, uint32_t size)
    {
#ifdef _WIN32
        if (!mDBRunning.load() || mDBServerSocket == INVALID_SOCKET || !data || size == 0)
        {
            return false;
        }

        std::lock_guard<std::mutex> lock(mDBSendMutex);

        const char* ptr = static_cast<const char*>(data);
        uint32_t remaining = size;
        while (remaining > 0)
        {
            int sent = send(mDBServerSocket, ptr, remaining, 0);
            if (sent == SOCKET_ERROR)
            {
                Logger::Error("Failed to send DB packet: " + std::to_string(WSAGetLastError()));
                return false;
            }
            remaining -= static_cast<uint32_t>(sent);
            ptr += sent;
        }

        return true;
#else
        (void)data;
        (void)size;
        return false;
#endif
    }

    void TestServer::DBRecvLoop()
    {
#ifdef _WIN32
        constexpr size_t kTempBufferSize = 4096;
        std::vector<char> tempBuffer(kTempBufferSize);

        while (mDBRunning.load())
        {
            int received = recv(mDBServerSocket, tempBuffer.data(),
                                static_cast<int>(tempBuffer.size()), 0);
            if (received > 0)
            {
                mDBRecvBuffer.insert(mDBRecvBuffer.end(),
                                     tempBuffer.data(),
                                     tempBuffer.data() + received);

                while (mDBRecvBuffer.size() - mDBRecvOffset >= sizeof(Core::ServerPacketHeader))
                {
                    const auto* header = reinterpret_cast<const Core::ServerPacketHeader*>(
                        mDBRecvBuffer.data() + mDBRecvOffset);

                    if (header->size < sizeof(Core::ServerPacketHeader) || header->size > 4096)
                    {
                        Logger::Warn("Invalid DB packet size: " + std::to_string(header->size));
                        mDBRecvBuffer.clear();
                        mDBRecvOffset = 0;
                        break;
                    }

                    if (mDBRecvBuffer.size() - mDBRecvOffset < header->size)
                    {
                        break;
                    }

                    if (mDBServerSession)
                    {
                        mDBServerSession->OnRecv(
                            mDBRecvBuffer.data() + mDBRecvOffset,
                            header->size);
                    }

                    mDBRecvOffset += header->size;
                }

                // English: Compact buffer when offset exceeds half the buffer size
                // 한글: 오프셋이 버퍼 크기의 절반을 초과하면 버퍼 압축
                if (mDBRecvOffset > 0 && mDBRecvOffset > mDBRecvBuffer.size() / 2)
                {
                    mDBRecvBuffer.erase(mDBRecvBuffer.begin(),
                                        mDBRecvBuffer.begin() + mDBRecvOffset);
                    mDBRecvOffset = 0;
                }
            }
            else if (received == 0)
            {
                Logger::Warn("DB server closed connection");
                break;
            }
            else
            {
                int error = WSAGetLastError();
                if (error == WSAEINTR)
                {
                    continue;
                }
                Logger::Error("DB recv failed: " + std::to_string(error));
                break;
            }
        }

        mDBRunning.store(false);

        // English: If server is still running and no reconnect is active, start one
        // 한글: 서버가 아직 실행 중이고 재연결 스레드가 없으면 시작
        if (mIsRunning.load() && !mDBReconnectRunning.load())
        {
            mDBReconnectRunning.store(true);
            if (mDBReconnectThread.joinable()) mDBReconnectThread.join();
            mDBReconnectThread = std::thread(&TestServer::DBReconnectLoop, this);
        }
#endif
    }

    void TestServer::DBPingLoop()
    {
#ifdef _WIN32
        constexpr uint32_t kPingIntervalMs = 5000;
        constexpr uint32_t kSaveInterval = 5;

        while (mDBRunning.load())
        {
            Core::PKT_ServerPingReq pingPacket;
            pingPacket.sequence = ++mDBPingSequence;
            pingPacket.timestamp = Timer::GetCurrentTimestamp();
            SendDBPacket(&pingPacket, sizeof(pingPacket));

            if (mDBPingSequence.load() % kSaveInterval == 0)
            {
                Core::PKT_DBSavePingTimeReq savePacket;
                savePacket.serverId = 1;
                savePacket.timestamp = Timer::GetCurrentTimestamp();
                strncpy_s(savePacket.serverName, sizeof(savePacket.serverName),
                          "TestServer", _TRUNCATE);
                SendDBPacket(&savePacket, sizeof(savePacket));
            }

            // English: Use condition variable wait instead of sleep_for so that
            //          DisconnectFromDBServer() can wake this thread immediately
            // 한글: DisconnectFromDBServer()가 즉시 깨울 수 있도록 sleep_for 대신
            //       조건 변수 wait 사용
            std::unique_lock<std::mutex> lock(mDBShutdownMutex);
            mDBShutdownCV.wait_for(lock,
                std::chrono::milliseconds(kPingIntervalMs),
                [this] { return !mDBRunning.load(); });
        }
#endif
    }

    void TestServer::DBReconnectLoop()
    {
#ifdef _WIN32
        // English: Exponential backoff: 1s, 2s, 4s, 8s, 16s, max 30s
        //          Exception: WSAECONNREFUSED (server shutting down / starting up)
        //          → short fixed 1s interval to catch fast restarts
        // 한글: 지수 백오프: 1초, 2초, 4초, 8초, 16초, 최대 30초
        //       예외: WSAECONNREFUSED (서버 종료 중 / 기동 중)
        //       → 빠른 재기동을 놓치지 않도록 1초 고정 간격 유지
        constexpr uint32_t kMaxDelayMs = 30000;
        constexpr uint32_t kConnRefusedDelayMs = 1000;
        uint32_t delayMs = 1000;
        int attempt = 0;

        while (mIsRunning.load() && !mDBRunning.load())
        {
            ++attempt;
            Logger::Info("DB reconnect attempt #" + std::to_string(attempt) +
                         " in " + std::to_string(delayMs) + "ms...");

            // English: Wait with CV so Stop() can interrupt immediately
            // 한글: Stop()이 즉시 중단할 수 있도록 CV로 대기
            {
                std::unique_lock<std::mutex> lock(mDBShutdownMutex);
                mDBShutdownCV.wait_for(lock,
                    std::chrono::milliseconds(delayMs),
                    [this] { return !mIsRunning.load(); });
            }

            if (!mIsRunning.load()) break;

            if (ConnectToDBServer(mDBHost, mDBPort))
            {
                Logger::Info("DB reconnected successfully after " +
                             std::to_string(attempt) + " attempt(s)");
                break;
            }

            // English: Distinguish WSAECONNREFUSED from other errors:
            //   WSAECONNREFUSED(10061): DB server is shutting down or starting up
            //   → Use short fixed interval (no backoff growth) to catch fast restarts
            //   Other errors: Apply standard exponential backoff
            // 한글: WSAECONNREFUSED와 기타 에러 구분:
            //   WSAECONNREFUSED(10061): DB 서버가 종료 중이거나 기동 중
            //   → 빠른 재기동 감지를 위해 짧은 고정 간격 유지 (백오프 증가 없음)
            //   기타 에러: 표준 지수 백오프 적용
            int lastError = mLastDBConnectError.load();
            if (lastError == WSAECONNREFUSED)
            {
                delayMs = kConnRefusedDelayMs;
                Logger::Info("DB server is shutting down or starting up (CONNREFUSED), retrying in 1s...");
            }
            else
            {
                delayMs = std::min(delayMs * 2, kMaxDelayMs);
            }
        }

        mDBReconnectRunning.store(false);
#endif
    }

} // namespace Network::TestServer
