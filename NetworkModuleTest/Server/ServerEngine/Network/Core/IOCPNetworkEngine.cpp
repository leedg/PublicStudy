// English: IOCPNetworkEngine implementation
// ?쒓?: IOCPNetworkEngine 援ы쁽
// encoding: UTF-8

#include "IOCPNetworkEngine.h"
#include <sstream>
#include <iostream>

#ifdef _WIN32
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")
#endif

namespace Network::Core
{

IOCPNetworkEngine::IOCPNetworkEngine()
    : mListenSocket(
#ifdef _WIN32
        INVALID_SOCKET
#else
        -1
#endif
    )
    , mPort(0)
    , mMaxConnections(0)
    , mRunning(false)
    , mInitialized(false)
    , mLogicThreadPool(4)
#ifdef _WIN32
    , mIOCP(nullptr)
#endif
{
    std::memset(&mStats, 0, sizeof(mStats));
}

IOCPNetworkEngine::~IOCPNetworkEngine()
{
    Stop();
}

// =============================================================================
// English: INetworkEngine interface
// ?쒓?: INetworkEngine ?명꽣?섏씠??
// =============================================================================

bool IOCPNetworkEngine::Initialize(size_t maxConnections, uint16_t port)
{
    if (mInitialized)
    {
        Utils::Logger::Warn("IOCPNetworkEngine already initialized");
        return false;
    }

    mPort = port;
    mMaxConnections = maxConnections;
    mStats.startTime = Utils::Timer::GetCurrentTimestamp();

    if (!InitializeWinsock())
    {
        return false;
    }

    if (!CreateListenSocket())
    {
        return false;
    }

    if (!CreateIOCP())
    {
        return false;
    }

    mInitialized = true;
    Utils::Logger::Info("IOCPNetworkEngine initialized on port " + std::to_string(mPort));
    return true;
}

bool IOCPNetworkEngine::Start()
{
    if (!mInitialized)
    {
        Utils::Logger::Error("IOCPNetworkEngine not initialized");
        return false;
    }

    if (mRunning)
    {
        Utils::Logger::Warn("IOCPNetworkEngine already running");
        return false;
    }

    mRunning = true;

    // English: Start IOCP worker threads (CPU core count)
    // ?쒓?: IOCP ?뚯빱 ?ㅻ젅???쒖옉 (CPU 肄붿뼱 ??
    uint32_t workerCount = std::thread::hardware_concurrency();
    if (workerCount == 0)
    {
        workerCount = 4;
    }

    for (uint32_t i = 0; i < workerCount; ++i)
    {
        // Use lambda to avoid member-function overload issues on some toolsets
        mWorkerThreads.emplace_back([this]() { this->WorkerThread(); });
    }

    // English: Start accept thread
    // ?쒓?: Accept ?ㅻ젅???쒖옉
    // Note: Full thread implementation deferred to platform-specific code

    Utils::Logger::Info("IOCPNetworkEngine started - Workers: " + std::to_string(workerCount));
    return true;
}

void IOCPNetworkEngine::Stop()
{
    if (!mRunning)
    {
        return;
    }

    mRunning = false;

    // English: Close listen socket to unblock accept
    // ?쒓?: accept 釉붾줈???댁젣瑜??꾪빐 listen ?뚯폆 ?リ린
#ifdef _WIN32
    if (mListenSocket != INVALID_SOCKET)
    {
        closesocket(mListenSocket);
        mListenSocket = INVALID_SOCKET;
    }
#endif

    if (mAcceptThread.joinable())
    {
        mAcceptThread.join();
    }

    // English: Post exit signals to IOCP workers
    // ?쒓?: IOCP ?뚯빱??醫낅즺 ?좏샇
#ifdef _WIN32
    if (mIOCP)
    {
        for (size_t i = 0; i < mWorkerThreads.size(); ++i)
        {
            PostQueuedCompletionStatus(mIOCP, 0, 0, nullptr);
        }
    }
#endif

    for (auto& thread : mWorkerThreads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }
    mWorkerThreads.clear();

    // English: Close all sessions
    // ?쒓?: 紐⑤뱺 ?몄뀡 醫낅즺
    SessionManager::Instance().CloseAllSessions();

#ifdef _WIN32
    if (mIOCP)
    {
        CloseHandle(mIOCP);
        mIOCP = nullptr;
    }

    WSACleanup();
#endif

    mInitialized = false;
    Utils::Logger::Info("IOCPNetworkEngine stopped");
}

bool IOCPNetworkEngine::IsRunning() const
{
    return mRunning;
}

bool IOCPNetworkEngine::RegisterEventCallback(NetworkEvent eventType, NetworkEventCallback callback)
{
    std::lock_guard<std::mutex> lock(mCallbackMutex);
    mCallbacks[eventType] = std::move(callback);
    return true;
}

void IOCPNetworkEngine::UnregisterEventCallback(NetworkEvent eventType)
{
    std::lock_guard<std::mutex> lock(mCallbackMutex);
    mCallbacks.erase(eventType);
}

bool IOCPNetworkEngine::SendData(Utils::ConnectionId connectionId, const void* data, size_t size)
{
    auto session = SessionManager::Instance().GetSession(connectionId);
    if (!session || !session->IsConnected())
    {
        return false;
    }

    session->Send(data, static_cast<uint32_t>(size));

    {
        std::lock_guard<std::mutex> lock(mStatsMutex);
        mStats.totalBytesSent += size;
    }

    return true;
}

void IOCPNetworkEngine::CloseConnection(Utils::ConnectionId connectionId)
{
    auto session = SessionManager::Instance().GetSession(connectionId);
    if (session)
    {
        session->Close();
        session->OnDisconnected();
        SessionManager::Instance().RemoveSession(connectionId);

        FireEvent(NetworkEvent::Disconnected, connectionId);
    }
}

std::string IOCPNetworkEngine::GetConnectionInfo(Utils::ConnectionId connectionId) const
{
    auto session = SessionManager::Instance().GetSession(connectionId);
    if (!session)
    {
        return "";
    }

    return "Session[" + std::to_string(connectionId) + "] State=" +
           std::to_string(static_cast<int>(session->GetState()));
}

INetworkEngine::Statistics IOCPNetworkEngine::GetStatistics() const
{
    std::lock_guard<std::mutex> lock(mStatsMutex);
    INetworkEngine::Statistics stats = {}
    ;
    // Copy fields explicitly to ensure matching type
    stats.totalConnections = mStats.totalConnections;
    stats.activeConnections = SessionManager::Instance().GetSessionCount();
    stats.totalBytesSent = mStats.totalBytesSent;
    stats.totalBytesReceived = mStats.totalBytesReceived;
    stats.totalErrors = mStats.totalErrors;
    stats.startTime = mStats.startTime;
    return stats;
}

// =============================================================================
// English: Internal initialization
// ?쒓?: ?대? 珥덇린??
// =============================================================================

bool IOCPNetworkEngine::InitializeWinsock()
{
#ifdef _WIN32
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0)
    {
        Utils::Logger::Error("WSAStartup failed - Error: " + std::to_string(result));
        return false;
    }
    Utils::Logger::Info("Winsock initialized");
    return true;
#else
    return true;
#endif
}

bool IOCPNetworkEngine::CreateListenSocket()
{
#ifdef _WIN32
    mListenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP,
                               nullptr, 0, WSA_FLAG_OVERLAPPED);

    if (mListenSocket == INVALID_SOCKET)
    {
        Utils::Logger::Error("Failed to create listen socket");
        return false;
    }

    // English: SO_REUSEADDR
    // ?쒓?: ?뚯폆 ?ъ궗???ㅼ젙
    int opt = 1;
    setsockopt(mListenSocket, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<char*>(&opt), sizeof(opt));

    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(mPort);

    if (bind(mListenSocket, reinterpret_cast<sockaddr*>(&serverAddr),
             sizeof(serverAddr)) == SOCKET_ERROR)
    {
        Utils::Logger::Error("Bind failed - Error: " + std::to_string(WSAGetLastError()));
        return false;
    }

    if (listen(mListenSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        Utils::Logger::Error("Listen failed - Error: " + std::to_string(WSAGetLastError()));
        return false;
    }

    Utils::Logger::Info("Listen socket created on port " + std::to_string(mPort));
    return true;
#else
    Utils::Logger::Error("Listen socket not implemented for this platform");
    return false;
#endif
}

bool IOCPNetworkEngine::CreateIOCP()
{
#ifdef _WIN32
    mIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (mIOCP == nullptr)
    {
        Utils::Logger::Error("Failed to create IOCP");
        return false;
    }
    Utils::Logger::Info("IOCP created");
    return true;
#else
    return false;
#endif
}

// =============================================================================
// English: Thread functions
// ?쒓?: ?ㅻ젅???⑥닔
// =============================================================================

void IOCPNetworkEngine::AcceptThread()
{
    Utils::Logger::Info("Accept thread started");

    while (mRunning)
    {
#ifdef _WIN32
        sockaddr_in clientAddr = {};
        int addrLen = sizeof(clientAddr);

        SocketHandle clientSocket = accept(mListenSocket,
                                            reinterpret_cast<sockaddr*>(&clientAddr),
                                            &addrLen);

        if (clientSocket == INVALID_SOCKET)
        {
            if (mRunning)
            {
                Utils::Logger::Warn("Accept failed - Error: " + std::to_string(WSAGetLastError()));
            }
            continue;
        }

        // English: Create session via SessionManager
        // ?쒓?: SessionManager瑜??듯빐 ?몄뀡 ?앹꽦
        SessionRef session = SessionManager::Instance().CreateSession(clientSocket);
        if (!session)
        {
            closesocket(clientSocket);
            continue;
        }

        // English: Associate socket with IOCP
        // ?쒓?: ?뚯폆??IOCP???깅줉
        if (CreateIoCompletionPort(reinterpret_cast<HANDLE>(clientSocket),
                                    mIOCP,
                                    static_cast<ULONG_PTR>(session->GetId()),
                                    0) == nullptr)
        {
            Utils::Logger::Error("Failed to associate socket with IOCP");
            SessionManager::Instance().RemoveSession(session);
            continue;
        }

        // English: Update stats
        // ?쒓?: ?듦퀎 ?낅뜲?댄듃
        {
            std::lock_guard<std::mutex> lock(mStatsMutex);
            mStats.totalConnections++;
        }

        // English: Fire Connected event asynchronously on logic thread
        // ?쒓?: 濡쒖쭅 ?ㅻ젅?쒖뿉??鍮꾨룞湲곕줈 Connected ?대깽??諛쒖깮
        auto sessionCopy = session;
        mLogicThreadPool.Submit([this, sessionCopy]()
        {
            sessionCopy->OnConnected();
            FireEvent(NetworkEvent::Connected, sessionCopy->GetId());
        });

        // English: Start receiving
        // ?쒓?: ?섏떊 ?쒖옉
        session->PostRecv();

        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, sizeof(clientIP));

        Utils::Logger::Info("Client connected - IP: " + std::string(clientIP) +
                            ":" + std::to_string(ntohs(clientAddr.sin_port)));
#endif
    }

    Utils::Logger::Info("Accept thread stopped");
}

void IOCPNetworkEngine::WorkerThread()
{
#ifdef _WIN32
    while (mRunning)
    {
        DWORD bytesTransferred = 0;
        ULONG_PTR completionKey = 0;
        OVERLAPPED* overlapped = nullptr;

        BOOL result = GetQueuedCompletionStatus(
            mIOCP,
            &bytesTransferred,
            &completionKey,
            &overlapped,
            INFINITE
        );

        // English: Exit signal (nullptr overlapped, 0 key)
        // ?쒓?: 醫낅즺 ?좏샇
        if (overlapped == nullptr)
        {
            break;
        }

        Utils::ConnectionId connId = static_cast<Utils::ConnectionId>(completionKey);
        SessionRef session = SessionManager::Instance().GetSession(connId);

        if (!session)
        {
            continue;
        }

        IOContext* ioContext = static_cast<IOContext*>(overlapped);

        if (!result || bytesTransferred == 0)
        {
            // English: Connection closed
            // ?쒓?: ?곌껐 醫낅즺
            auto sessionCopy = session;
            mLogicThreadPool.Submit([this, sessionCopy]()
            {
                sessionCopy->OnDisconnected();
                FireEvent(NetworkEvent::Disconnected, sessionCopy->GetId());
            });

            session->Close();
            SessionManager::Instance().RemoveSession(session);
            continue;
        }

        // English: Process IO completion
        // ?쒓?: IO ?꾨즺 泥섎━
        switch (ioContext->type)
        {
        case IOType::Recv:
            ProcessRecvCompletion(session, bytesTransferred);
            break;

        case IOType::Send:
            ProcessSendCompletion(session, bytesTransferred);
            break;

        default:
            break;
        }
    }
#endif
}

void IOCPNetworkEngine::ProcessRecvCompletion(SessionRef session, uint32_t bytesTransferred)
{
    if (!session || !session->IsConnected())
    {
        return;
    }

    // English: Copy received data for async processing
    // ?쒓?: 鍮꾨룞湲?泥섎━瑜??꾪빐 ?섏떊 ?곗씠??蹂듭궗
#ifdef _WIN32
    std::vector<char> data(session->GetRecvContext().buffer,
                           session->GetRecvContext().buffer + bytesTransferred);
#else
    std::vector<char> data;
#endif

    // English: Update stats
    // ?쒓?: ?듦퀎 ?낅뜲?댄듃
    {
        std::lock_guard<std::mutex> lock(mStatsMutex);
        mStats.totalBytesReceived += bytesTransferred;
    }

    // English: Process on logic thread (async)
    // ?쒓?: 濡쒖쭅 ?ㅻ젅?쒖뿉??泥섎━ (鍮꾨룞湲?
    auto sessionCopy = session;
    mLogicThreadPool.Submit([this, sessionCopy, data = std::move(data)]()
    {
        sessionCopy->OnRecv(data.data(), static_cast<uint32_t>(data.size()));

        // English: Fire DataReceived event
        // ?쒓?: DataReceived ?대깽??諛쒖깮
        FireEvent(NetworkEvent::DataReceived, sessionCopy->GetId(),
                  reinterpret_cast<const uint8_t*>(data.data()), data.size());
    });

    // English: Post next receive
    // ?쒓?: ?ㅼ쓬 ?섏떊 ?깅줉
    session->PostRecv();
}

void IOCPNetworkEngine::ProcessSendCompletion(SessionRef session, uint32_t bytesTransferred)
{
    if (!session || !session->IsConnected())
    {
        return;
    }

    // English: Send completion is handled by Session internally
    // ?쒓?: ?꾩넚 ?꾨즺??Session ?대??먯꽌 泥섎━
    FireEvent(NetworkEvent::DataSent, session->GetId());
}

void IOCPNetworkEngine::FireEvent(NetworkEvent eventType, Utils::ConnectionId connId,
                                   const uint8_t* data, size_t dataSize, OSError errorCode)
{
    std::lock_guard<std::mutex> lock(mCallbackMutex);

    auto it = mCallbacks.find(eventType);
    if (it != mCallbacks.end())
    {
        NetworkEventData eventData;
        eventData.eventType = eventType;
        eventData.connectionId = connId;
        eventData.dataSize = dataSize;
        eventData.errorCode = errorCode;
        eventData.timestamp = Utils::Timer::GetCurrentTimestamp();

        if (data && dataSize > 0)
        {
            eventData.data = std::make_unique<uint8_t[]>(dataSize);
            std::memcpy(eventData.data.get(), data, dataSize);
        }

        it->second(eventData);
    }
}

// =============================================================================
// English: Factory function implementation
// ?쒓?: ?⑺넗由??⑥닔 援ы쁽
// =============================================================================

std::unique_ptr<INetworkEngine> CreateNetworkEngine(const std::string& engineType)
{
#ifdef _WIN32
    return std::unique_ptr<INetworkEngine>(new IOCPNetworkEngine());
#else
    Utils::Logger::Error("No network engine available for this platform");
    return nullptr;
#endif
}

std::vector<std::string> GetAvailableEngineTypes()
{
    std::vector<std::string> types;
#ifdef _WIN32
    types.push_back("iocp");
#endif
    return types;
}

} // namespace Network::Core

