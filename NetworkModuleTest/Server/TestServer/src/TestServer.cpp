// English: TestServer implementation with separated handlers
// Korean: 분리된 핸들러를 가진 TestServer 구현

#include "../include/TestServer.h"
#include <iostream>

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
    {
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

        // English: Shutdown DB task queue first (wait for pending tasks)
        // Korean: DB 작업 큐를 먼저 종료 (대기 중인 작업 완료 대기)
        if (mDBTaskQueue)
        {
            Logger::Info("Shutting down DB task queue...");
            mDBTaskQueue->Shutdown();
            Logger::Info("DB task queue statistics - Processed: " +
                        std::to_string(mDBTaskQueue->GetProcessedCount()) +
                        ", Failed: " + std::to_string(mDBTaskQueue->GetFailedCount()));
        }

        if (mClientEngine)
        {
            mClientEngine->Stop();
        }

        // English: Disconnect from DB server if connected
        // Korean: DB 서버에 연결되어 있다면 연결 해제
        if (mDBServerSession)
        {
            mDBServerSession->Close();
            mDBServerSession.reset();
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
        // English: Initialize Winsock if not already initialized
        // 한글: Winsock이 초기화되지 않았다면 초기화
        WSADATA wsaData;
        static bool wsaInitialized = false;
        if (!wsaInitialized)
        {
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
            {
                Logger::Error("WSAStartup failed");
                return false;
            }
            wsaInitialized = true;
        }

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

        // English: Set socket to non-blocking mode
        // 한글: 소켓을 non-blocking 모드로 설정
        u_long mode = 1;
        if (ioctlsocket(clientSocket, FIONBIO, &mode) != 0)
        {
            Logger::Error("Failed to set non-blocking mode: " + std::to_string(WSAGetLastError()));
            closesocket(clientSocket);
            return false;
        }

        // English: Create and initialize session for DB server connection
        // 한글: DB 서버 연결을 위한 세션 생성 및 초기화
        mDBServerSession = std::make_shared<Core::Session>();
        mDBServerSession->Initialize(static_cast<uint64_t>(clientSocket), clientSocket);
        
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

} // namespace Network::TestServer
