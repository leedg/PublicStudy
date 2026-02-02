#pragma once

// English: IOCP-based INetworkEngine implementation
// 한글: IOCP 기반 INetworkEngine 구현

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
    // 한글: IOCPNetworkEngine - IOCP 사용 INetworkEngine 구현체
    // =============================================================================

    class IOCPNetworkEngine : public INetworkEngine
    {
    public:
        IOCPNetworkEngine();
        virtual ~IOCPNetworkEngine();

        // =====================================================================
        // English: INetworkEngine interface implementation
        // 한글: INetworkEngine 인터페이스 구현
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
        // 한글: 로직 스레드 풀 접근
        // =====================================================================

        Utils::ThreadPool& GetLogicThreadPool() { return mLogicThreadPool; }

    private:
        // English: Internal initialization
        // 한글: 내부 초기화
        bool InitializeWinsock();
        bool CreateListenSocket();
        bool CreateIOCP();

        // English: Thread functions
        // 한글: 스레드 함수
        void AcceptThread();
        void WorkerThread();

        // English: IO completion handlers
        // 한글: IO 완료 핸들러
        void ProcessRecvCompletion(SessionRef session, uint32_t bytesTransferred);
        void ProcessSendCompletion(SessionRef session, uint32_t bytesTransferred);

        // English: Fire event callback
        // 한글: 이벤트 콜백 호출
        void FireEvent(NetworkEvent eventType, Utils::ConnectionId connId,
                       const uint8_t* data = nullptr, size_t dataSize = 0,
                       OSError errorCode = 0);

    private:
        // English: Socket & IOCP handles
        // 한글: 소켓 & IOCP 핸들
        SocketHandle            mListenSocket;
        uint16_t                mPort;
        size_t                  mMaxConnections;

#ifdef _WIN32
        HANDLE                  mIOCP;
#endif

        // English: Threads
        // 한글: 스레드
        std::thread             mAcceptThread;
        std::vector<std::thread> mWorkerThreads;
        std::atomic<bool>       mRunning;
        std::atomic<bool>       mInitialized;

        // English: Logic thread pool (for async business logic)
        // 한글: 로직 스레드 풀 (비동기 비즈니스 로직용)
        Utils::ThreadPool       mLogicThreadPool;

        // English: Event callbacks
        // 한글: 이벤트 콜백
        std::unordered_map<NetworkEvent, NetworkEventCallback> mCallbacks;
        std::mutex              mCallbackMutex;

        // English: Statistics
        // 한글: 통계
        mutable std::mutex      mStatsMutex;
        INetworkEngine::Statistics              mStats;
    };

} // namespace Network::Core
