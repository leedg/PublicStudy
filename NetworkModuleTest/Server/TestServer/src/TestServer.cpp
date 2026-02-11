// English: TestServer implementation with separated handlers
// Korean: 분리된 핸들러를 가진 TestServer 구현

#include "../include/TestServer.h"
#include "Network/Core/ServerPacketDefine.h"
#include <iostream>
#include <mutex>
#include <thread>
#include <chrono>
#include <cstring>

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

        // English: Set Session factory for game clients
        // Korean: 게임 클라이언트용 세션 팩토리 설정
        Core::SessionManager::Instance().Initialize(&TestServer::CreateGameSession);

        // English: Initialize DB packet handler for server-to-server communication
        // Korean: 서버 간 통신을 위한 DB 패킷 핸들러 초기화
        mDBPacketHandler = std::make_unique<DBServerPacketHandler>();

        // English: Initialize asynchronous DB task queue (independent worker threads)
        // Korean: 비동기 DB 작업 큐 초기화 (독립 워커 스레드)
        mDBTaskQueue = std::make_unique<DBTaskQueue>();
        if (!mDBTaskQueue->Initialize(2))  // 2 worker threads for DB operations
        {
            Logger::Error("Failed to initialize DB task queue");
            return false;
        }

        // English: Inject DB task queue into GameSession (dependency injection)
        // Korean: GameSession에 DB 작업 큐 주입 (의존성 주입)
        GameSession::SetDBTaskQueue(mDBTaskQueue.get());

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
        if (mDBRunning.load())
        {
            Logger::Warn("DB server connection already running");
            return true;
        }

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
            Logger::Error("Failed to create client socket: " + std::to_string(WSAGetLastError()));
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
            Logger::Error("Failed to connect to DB server: " + std::to_string(WSAGetLastError()));
            closesocket(clientSocket);
            return false;
        }

        // English: Create and initialize session for DB server connection
        // 한글: DB 서버 연결을 위한 세션 생성 및 초기화
        mDBServerSession = std::make_shared<Core::Session>();
        mDBServerSession->Initialize(static_cast<uint64_t>(clientSocket), clientSocket);

        mDBServerSocket = clientSocket;
        mDBRunning.store(true);

        // English: Start DB recv/ping threads
        // 한글: DB 수신/핑 스레드 시작
        mDBRecvThread = std::thread(&TestServer::DBRecvLoop, this);
        mDBPingThread = std::thread(&TestServer::DBPingLoop, this);
        
        // English: Initialize packet handler for DB server
        // 한글: DB 서버용 패킷 핸들러 초기화
        if (!mDBPacketHandler)
        {
            mDBPacketHandler = std::make_unique<DBServerPacketHandler>();
        }

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

    SessionRef TestServer::CreateGameSession()
    {
        return std::make_shared<GameSession>();
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

                    if (mDBPacketHandler && mDBServerSession)
                    {
                        mDBPacketHandler->ProcessPacket(
                            mDBServerSession.get(),
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

} // namespace Network::TestServer
