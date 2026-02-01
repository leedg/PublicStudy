#pragma once

// English: IOCP-based INetworkEngine implementation
// ?쒓?: IOCP 湲곕컲 INetworkEngine 援ы쁽

#include "NetworkEngine.h"
#include "Session.h"
#include "SessionManager.h"
#include "../../Utils/NetworkUtils.h"
#include <vector>
#include <thread>
#include <atomic>

namespace Network::Core
{
    // =============================================================================
    // English: IOCPNetworkEngine - concrete INetworkEngine using IOCP
    // ?쒓?: IOCPNetworkEngine - IOCP ?ъ슜 INetworkEngine 援ы쁽泥?
    // =============================================================================

    class IOCPNetworkEngine : public INetworkEngine
    {
    public:
        IOCPNetworkEngine();
        virtual ~IOCPNetworkEngine();

        // =====================================================================
        // English: INetworkEngine interface implementation
        // ?쒓?: INetworkEngine ?명꽣?섏씠??援ы쁽
        // =====================================================================

        bool Initialize(size_t maxConnections, uint16_t port) override;
        bool Start() override;
        void Stop() override;
        bool IsRunning() const override;

        bool RegisterEventCallback(NetworkEvent eventType, NetworkEventCallback callback) override;
        void UnregisterEventCallback(NetworkEvent eventType) override;

        bool SendData(Utils::ConnectionId connectionId, const void* data, size_t size) override;
        void CloseConnection(Utils::ConnectionId connectionId) override;
        std::string GetConnectionInfo(Utils::ConnectionId connectionId) const override;

        INetworkEngine::Statistics GetStatistics() const override;

        // =====================================================================
        // English: Logic thread pool access
        // ?쒓?: 濡쒖쭅 ?ㅻ젅??? ?묎렐
        // =====================================================================

        Utils::ThreadPool& GetLogicThreadPool() { return mLogicThreadPool; }

    private:
        // English: Internal initialization
        // ?쒓?: ?대? 珥덇린??
        bool InitializeWinsock();
        bool CreateListenSocket();
        bool CreateIOCP();

        // English: Thread functions
        // ?쒓?: ?ㅻ젅???⑥닔
        void AcceptThread();
        void WorkerThread();

        // English: IO completion handlers
        // ?쒓?: IO ?꾨즺 ?몃뱾??
        void ProcessRecvCompletion(SessionRef session, uint32_t bytesTransferred);
        void ProcessSendCompletion(SessionRef session, uint32_t bytesTransferred);

        // English: Fire event callback
        // ?쒓?: ?대깽??肄쒕갚 ?몄텧
        void FireEvent(NetworkEvent eventType, Utils::ConnectionId connId,
                       const uint8_t* data = nullptr, size_t dataSize = 0,
                       OSError errorCode = 0);

    private:
        // English: Socket & IOCP handles
        // ?쒓?: ?뚯폆 & IOCP ?몃뱾
        SocketHandle            mListenSocket;
        uint16_t                mPort;
        size_t                  mMaxConnections;

#ifdef _WIN32
        HANDLE                  mIOCP;
#endif

        // English: Threads
        // ?쒓?: ?ㅻ젅??
        std::thread             mAcceptThread;
        std::vector<std::thread> mWorkerThreads;
        std::atomic<bool>       mRunning;
        std::atomic<bool>       mInitialized;

        // English: Logic thread pool (for async business logic)
        // ?쒓?: 濡쒖쭅 ?ㅻ젅??? (鍮꾨룞湲?鍮꾩쫰?덉뒪 濡쒖쭅??
        Utils::ThreadPool       mLogicThreadPool;

        // English: Event callbacks
        // ?쒓?: ?대깽??肄쒕갚
        std::unordered_map<NetworkEvent, NetworkEventCallback> mCallbacks;
        std::mutex              mCallbackMutex;

        // English: Statistics
        // ?쒓?: ?듦퀎
        mutable std::mutex      mStatsMutex;
        INetworkEngine::Statistics              mStats;
    };

} // namespace Network::Core

