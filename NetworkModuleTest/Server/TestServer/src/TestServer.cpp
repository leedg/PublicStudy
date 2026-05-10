// TestServer 구현 — 클라이언트 연결과 DB 서버 연결을 함께 관리

#include "../include/TestServer.h"
#include "Network/Core/ServerPacketDefine.h"
// 로컬 DB 인스턴스 생성에 필요한 IDatabase / DatabaseFactory 전체 정의
#include "Interfaces/IDatabase.h"
#include "Database/DatabaseFactory.h"
#include <mutex>
#include <thread>
#include <chrono>
#include "Utils/StringUtil.h"
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
    // TestServer 구현
    // =============================================================================

    TestServer::TestServer()
        : mPacketHandler(std::make_unique<ClientPacketHandler>())
        , mIsRunning(false)
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

    // =============================================================================
    // DB 핑 헬퍼 — ConnectToDBServer()에서 등록한 TimerQueue가 PING_INTERVAL_MS마다 호출.
    //   kSaveInterval(5)회 핑마다 DBSavePingTimeReq를 추가로 전송하여 DB에 시간을 기록한다.
    // =============================================================================

    void TestServer::SendDBPing()
    {
#ifdef _WIN32
        constexpr uint32_t kSaveInterval = 5;

        Core::PKT_ServerPingReq pingPacket;
        pingPacket.sequence  = ++mDBPingSequence;
        pingPacket.timestamp = Timer::GetCurrentTimestamp();
        SendDBPacket(&pingPacket, sizeof(pingPacket));

        if (mDBPingSequence.load() % kSaveInterval == 0)
        {
            Core::PKT_DBSavePingTimeReq savePacket;
            savePacket.serverId  = 1;
            savePacket.timestamp = Timer::GetCurrentTimestamp();
            Network::Utils::StringUtil::Copy(savePacket.serverName, "TestServer");
            SendDBPacket(&savePacket, sizeof(savePacket));
        }
#endif
    }

    TestServer::~TestServer()
    {
        if (mIsRunning.load())
        {
            Stop();
        }
    }

    bool TestServer::Initialize(uint16_t port,
                                const std::string& dbConnectionString,
                                const std::string& engineType,
                                size_t             dbWorkerCount)
    {
        mPort = port;
        mDbConnectionString = dbConnectionString;
        mEngineType = engineType.empty() ? "auto" : engineType;

        // 비동기 DB 작업 큐를 먼저 초기화 (세션 팩토리에서 필요).
        //   DBTaskQueue는 sessionId % workerCount로 라우팅하므로 동일 세션 작업은
        //   항상 같은 워커에 배정됨 (워커 내 FIFO 보장).
        //   기본값 = DEFAULT_TASK_QUEUE_WORKER_COUNT(3); CLI -w로 재설정 가능.
        //   3 워커는 단일 머신 환경에서 세션 친화도 순서 보장을 유지하면서
        //   충분한 병렬 처리량을 제공하는 균형점이다.
        //
        // 로컬 DB 생성 — 경로가 있으면 SQLite, 없으면 MockDatabase.
        //   소유 인스턴스를 DBTaskQueue에 주입하여 접속/해제/플레이어 데이터를
        //   별도 DB 서버 없이도 저장할 수 있다.
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
        if (!mDBTaskQueue->Initialize(dbWorkerCount, "db_tasks.wal", mLocalDatabase.get()))
        {
            Logger::Error("Failed to initialize DB task queue");
            return false;
        }

        // DBServerTaskQueue 초기화 — 클라이언트 요청을 TestDBServer로 중계
        mDBServerTaskQueue = std::make_shared<DBServerTaskQueue>();
        if (!mDBServerTaskQueue->Initialize(
                1,   // 1 worker is sufficient for initial deployment
                [this](const void* data, uint32_t size) -> bool {
                    return SendDBPacket(data, size);
                }))
        {
            Logger::Error("Failed to initialize DBServerTaskQueue");
            return false;
        }

#ifdef _WIN32
        // DB 핑 타이머 큐 초기화 (백그라운드 스레드 1개, 여기서 시작).
        if (!mTimerQueue.Initialize())
        {
            Logger::Error("TestServer: TimerQueue initialization failed");
            return false;
        }
#endif

        // SessionConfigurator로 세션별 recv 콜백 등록.
        //   CreateSession 내에서 PostRecv() 이전에 호출되므로 첫 recv 완료가
        //   반드시 콜백을 인식함 (경합 없음).
        //   ClientPacketHandler는 공유하며 생성 후 stateless — 스레드 안전.
        {
            ClientPacketHandler* handlerPtr = mPacketHandler.get();
            Core::SessionManager::Instance().SetSessionConfigurator(
                [handlerPtr](Core::Session* session)
                {
                    session->SetOnRecv(
                        [handlerPtr](Core::Session* s, const char* data, uint32_t size)
                        {
                            handlerPtr->ProcessPacket(s, data, size);
                        });
                });
        }

        // 선택한 백엔드로 클라이언트 네트워크 엔진 생성 및 초기화.
        // "auto"는 플랫폼 기본 자동 선택 동작을 유지.
        mClientEngine = CreateNetworkEngine(mEngineType);
        if (!mClientEngine)
        {
            Logger::Error("Failed to create network engine: " + mEngineType);
            return false;
        }

        constexpr size_t kMaxConnections = Utils::MAX_CONNECTIONS;
        if (!mClientEngine->Initialize(kMaxConnections, port))
        {
            Logger::Error("Failed to initialize client network engine");
            return false;
        }

        // 클라이언트 연결 이벤트 콜백 등록
        mClientEngine->RegisterEventCallback(NetworkEvent::Connected,
            [this](const NetworkEventData& e) { OnClientConnectionEstablished(e); });

        mClientEngine->RegisterEventCallback(NetworkEvent::Disconnected,
            [this](const NetworkEventData& e) { OnClientConnectionClosed(e); });

        mClientEngine->RegisterEventCallback(NetworkEvent::DataReceived,
            [this](const NetworkEventData& e) { OnClientDataReceived(e); });

        Logger::Info("TestServer initialized on port " + std::to_string(port) +
                     " (engine: " + mEngineType + ")");
        return true;
    }

    // =============================================================================
    // RunSelfTest — DBServerTaskQueue check-failure 경로 검증 (네트워크 불필요)
    // =============================================================================

    bool TestServer::RunSelfTest()
    {
        if (!mDBServerTaskQueue || !mDBServerTaskQueue->IsRunning())
        {
            Logger::Error("RunSelfTest: DBServerTaskQueue not initialized");
            return false;
        }

        Logger::Info("=== DBServerTaskQueue SelfTest: Check-failure path ===");

        // 테스트 1: data가 비면 SavePlayerProgress는 InvalidRequest 반환해야 함
        std::atomic<bool> gotCallback{false};
        bool              passed{false};

        DBServerTask task;
        task.type      = DBServerTaskType::SavePlayerProgress;
        task.sessionId = 9999;   // arbitrary test session
        task.data      = "";     // empty → CheckTask must reject
        task.callback  = [&](ResultCode rc, const std::string& msg)
        {
            Logger::Info("[SelfTest] Check-fail callback: rc=" +
                         std::string(Network::ToString(rc)) + " msg=" + msg);
            passed = (rc == ResultCode::InvalidRequest);
            gotCallback.store(true);
        };
        mDBServerTaskQueue->EnqueueTask(std::move(task));

        // 워커가 처리할 때까지 최대 1초 대기
        for (int i = 0; i < 100 && !gotCallback.load(); ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        if (!gotCallback.load())
        {
            Logger::Error("[SelfTest] FAIL: callback not received within 1s");
            return false;
        }

        if (!passed)
        {
            Logger::Error("[SelfTest] FAIL: expected InvalidRequest, got different ResultCode");
            return false;
        }

        Logger::Info("[SelfTest] PASS: empty-data check correctly returns InvalidRequest");
        Logger::Info("=== DBServerTaskQueue SelfTest complete ===");
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

#ifdef _WIN32
        // 재연결 루프가 백오프 대기 중이면 즉시 깨움
        mDBShutdownCV.notify_all();

        if (mDBReconnectThread.joinable())
        {
            mDBReconnectThread.join();
        }
#endif

        // Graceful shutdown 순서:
        //   1) DBTaskQueue drain 전에 남은 세션의 disconnect 기록을 먼저 큐잉
        //      → Stop() 중 마지막 기록이 누락되지 않도록 보장
        //   2) DBTaskQueue drain → local DB 해제
        //   3) DB 서버 소켓 해제 (WSACleanup 이전에 반드시 먼저)
        //   4) 네트워크 엔진 종료 (WSACleanup 포함)
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
            // 세션 제거와의 경합 조건 방지를 위해 스냅샷 사용
            auto allSessions = Core::SessionManager::Instance().GetAllSessions();
            for (auto& session : allSessions)
            {
                if (!session || !session->IsConnected())
                {
                    continue;
                }

                mDBTaskQueue->RecordDisconnectTime(session->GetId(), shutdownTime);
                ++queuedDisconnectCount;
            }

            if (queuedDisconnectCount > 0)
            {
                Logger::Info("Queued disconnect records during shutdown: " +
                             std::to_string(queuedDisconnectCount));
            }
        }

        // 연결 해제 전 DB 핑 타이머 종료 (마지막 핑이 누락되지 않도록)
#ifdef _WIN32
        if (mDBPingTimer != 0)
        {
            mTimerQueue.Cancel(mDBPingTimer);
            mDBPingTimer = 0;
        }
        mTimerQueue.Shutdown();
#endif

        // 1단계 — DB 태스크 큐 드레인 (로컬 DB 연결이 살아있는 동안 대기 작업 완료)
        if (mDBTaskQueue)
        {
            Logger::Info("Shutting down DB task queue...");
            mDBTaskQueue->Shutdown();
            Logger::Info("DB task queue statistics - Processed: " +
                        std::to_string(mDBTaskQueue->GetProcessedCount()) +
                        ", Failed: " + std::to_string(mDBTaskQueue->GetFailedCount()));
        }

        // 큐가 완전히 드레인된 후 로컬 DB 연결 해제
        if (mLocalDatabase)
        {
            mLocalDatabase->Disconnect();
            mLocalDatabase.reset();
            Logger::Info("TestServer: local database disconnected");
        }

        // 2단계 — DB 서버 소켓을 mClientEngine->Stop() 이전에 해제.
        //   mClientEngine->Stop()이 WSACleanup()을 호출하면 mDBServerSocket이 무효화된다.
        //   WSACleanup 이후에 소켓을 닫으면 DBRecvLoop에서 WSAECONNRESET(10054)이 발생한다.
        DisconnectFromDBServer();

        // DBRecvThread join 후 DBServerTaskQueue 종료
        if (mDBServerTaskQueue && mDBServerTaskQueue->IsRunning())
        {
            Logger::Info("Shutting down DBServerTaskQueue...");
            mDBServerTaskQueue->Shutdown();
        }

        // 3단계 — 클라이언트 네트워크 엔진 종료 (IOCP/RIO 종료, WSACleanup 호출)
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
        // 재연결 루프에서 사용할 엔드포인트 저장
        mDBHost = host;
        mDBPort = port;

        if (mDBRunning.load())
        {
            Logger::Warn("DB server connection already running");
            return true;
        }

        // 재연결 경로에서 재사용 전 이전 recv 스레드를 join.
        // 핑은 TimerQueue가 처리 — 이전 타이머 취소.
        if (mDBRecvThread.joinable()) mDBRecvThread.join();
        if (mDBPingTimer != 0)
        {
            mTimerQueue.Cancel(mDBPingTimer);
            mDBPingTimer = 0;
        }

        // Winsock을 정확히 한 번 초기화 (call_once로 스레드 안전)
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

        // 클라이언트 소켓 생성
        SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (clientSocket == INVALID_SOCKET)
        {
            mLastDBConnectError.store(WSAGetLastError());
            Logger::Error("Failed to create client socket: " + std::to_string(mLastDBConnectError.load()));
            return false;
        }

        // 서버 주소 설정
        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);

        // 호스트 문자열을 주소로 변환
        if (inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr) <= 0)
        {
            Logger::Error("Invalid address: " + host);
            closesocket(clientSocket);
            return false;
        }

        // DB 서버에 연결
        if (connect(clientSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR)
        {
            mLastDBConnectError.store(WSAGetLastError());
            Logger::Error("Failed to connect to DB server: " + std::to_string(mLastDBConnectError.load()));
            closesocket(clientSocket);
            return false;
        }

        // 성공 시 마지막 에러 초기화
        mLastDBConnectError.store(0);

        // DB 연결을 위한 DBServerSession 생성 및 초기화
        mDBServerSession = std::make_shared<DBServerSession>();
        // DBQueryRes 응답 라우팅을 위해 DBServerTaskQueue 주입
        if (mDBServerTaskQueue)
        {
            mDBServerSession->GetPacketHandler()->SetTaskQueue(mDBServerTaskQueue.get());
        }
        mDBServerSession->Initialize(static_cast<uint64_t>(clientSocket), clientSocket);

        mDBServerSocket = clientSocket;
        mDBRunning.store(true);

        // DB 수신 스레드 시작.
        //   DB 핑은 TimerQueue가 담당 (PING_INTERVAL_MS마다 발동).
        //   반복 콜백이 mDBRunning을 반환하므로 연결 해제 시 타이머 자동 취소.
        mDBRecvThread = std::thread(&TestServer::DBRecvLoop, this);
        mDBPingTimer  = mTimerQueue.ScheduleRepeat(
            [this]() -> bool
            {
                SendDBPing();
                return mDBRunning.load();
            },
            Core::PING_INTERVAL_MS);

        Logger::Info("Successfully connected to DB server at " + host + ":" + std::to_string(port));
        return true;
#else
        // Windows가 아닌 플랫폼에서는 아직 클라이언트 연결을 지원하지 않음
        Logger::Error("Client connection not implemented for non-Windows platforms");
        return false;
#endif
    }

    void TestServer::OnClientConnectionEstablished(const NetworkEventData& eventData)
    {
        Logger::Info("Client connected - Connection: " + std::to_string(eventData.connectionId));

        // 접속 시간 비동기 기록.
        //   OnClientConnectionEstablished는 IOCP 완료 스레드에서 호출되므로
        //   블로킹 DB 호출 대신 DBTaskQueue에 비동기 작업으로 위임한다.
        if (mDBTaskQueue && mDBTaskQueue->IsRunning())
        {
            auto now = std::chrono::system_clock::now();
            auto rawTime = std::chrono::system_clock::to_time_t(now);
            std::tm localTime{};
#ifdef _WIN32
            localtime_s(&localTime, &rawTime);
#else
            localtime_r(&rawTime, &localTime);
#endif
            char timeStr[64] = {0};
            std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &localTime);
            mDBTaskQueue->RecordConnectTime(eventData.connectionId, timeStr);
        }
    }

    void TestServer::OnClientConnectionClosed(const NetworkEventData& eventData)
    {
        Logger::Info("Client disconnected - Connection: " + std::to_string(eventData.connectionId));

        // 정상 운영 중에만 접속 종료 시간 기록.
        //   Stop()이 이미 엔진 종료 전에 모든 활성 세션의 종료 기록을 큐잉하므로,
        //   종료 중에는 중복 방지를 위해 건너뛴다.
        if (mIsRunning.load() && mDBTaskQueue && mDBTaskQueue->IsRunning())
        {
            auto now = std::chrono::system_clock::now();
            auto rawTime = std::chrono::system_clock::to_time_t(now);
            std::tm localTime{};
#ifdef _WIN32
            localtime_s(&localTime, &rawTime);
#else
            localtime_r(&rawTime, &localTime);
#endif
            char timeStr[64] = {0};
            std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &localTime);
            mDBTaskQueue->RecordDisconnectTime(eventData.connectionId, timeStr);
        }
    }

    void TestServer::OnClientDataReceived(const NetworkEventData& eventData)
    {
        Logger::Debug("Received " + std::to_string(eventData.dataSize) +
            " bytes from client Connection: " + std::to_string(eventData.connectionId));
    }

    void TestServer::DisconnectFromDBServer()
    {
#ifdef _WIN32
        if (!mDBRunning.load())
        {
            return;
        }

        mDBRunning.store(false);

        // DB 핑 타이머 즉시 취소 (반환값 기반 자동 취소는 한 주기 지연될 수 있음)
        if (mDBPingTimer != 0)
        {
            mTimerQueue.Cancel(mDBPingTimer);
            mDBPingTimer = 0;
        }

        // 재연결 루프가 백오프 대기 중이면 즉시 깨움
        mDBShutdownCV.notify_all();

        if (mDBServerSocket != INVALID_SOCKET)
        {
            shutdown(mDBServerSocket, SD_BOTH);
        }

        if (mDBRecvThread.joinable())
        {
            mDBRecvThread.join();
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

                    auto sessionSnapshot = mDBServerSession;  // 로컬 스냅샷 생성
                    if (sessionSnapshot)
                    {
                        sessionSnapshot->OnRecv(
                            mDBRecvBuffer.data() + mDBRecvOffset,
                            header->size);
                    }

                    mDBRecvOffset += header->size;
                }

                // 오프셋이 버퍼 크기의 절반을 초과하면 버퍼 압축
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

        // 서버가 아직 실행 중이고 재연결 스레드가 없으면 시작
        if (mIsRunning.load() && !mDBReconnectRunning.load())
        {
            mDBReconnectRunning.store(true);
            if (mDBReconnectThread.joinable()) mDBReconnectThread.join();
            mDBReconnectThread = std::thread(&TestServer::DBReconnectLoop, this);
        }
#endif
    }

    // DBPingLoop() 제거 — DB 핑은 ConnectToDBServer()의 mTimerQueue.ScheduleRepeat()가 담당.
    // 핑 로직은 SendDBPing() 참조.

    void TestServer::DBReconnectLoop()
    {
#ifdef _WIN32
        // 지수 백오프: 1s → 2s → 4s → 8s → 16s, 최대 30s(kMaxDelayMs).
        //   각 실패마다 대기 시간을 2배씩 늘려 DB 서버 과부하를 방지한다.
        //   단, WSAECONNREFUSED(10061, 서버 종료/기동 중)는 예외:
        //   빠른 재기동을 놓치지 않도록 kConnRefusedDelayMs(1s) 고정 간격을 유지한다.
        //   mDBShutdownCV.wait_for()를 사용하여 Stop() 호출 시 즉시 탈출한다.
        constexpr uint32_t kMaxDelayMs = 30000;
        constexpr uint32_t kConnRefusedDelayMs = 1000;
        uint32_t delayMs = 1000;
        int attempt = 0;

        while (mIsRunning.load() && !mDBRunning.load())
        {
            ++attempt;
            Logger::Info("DB reconnect attempt #" + std::to_string(attempt) +
                         " in " + std::to_string(delayMs) + "ms...");

            // Stop()이 즉시 중단할 수 있도록 CV로 대기
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

            // WSAECONNREFUSED(10061): DB 서버가 종료 중이거나 기동 중
            //   → 빠른 재기동 감지를 위해 짧은 고정 간격 유지 (백오프 증가 없음)
            // 기타 에러: 표준 지수 백오프 적용 (delayMs *= 2, 최대 kMaxDelayMs)
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
