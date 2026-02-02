#pragma once

// English: TestClient - synchronous TCP client for testing game server
// 한글: TestClient - 게임 서버 테스트용 동기 TCP 클라이언트

#include "Network/Core/PacketDefine.h"
#include "Utils/NetworkUtils.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>

#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <cstdint>

namespace Network::TestClient
{
    using Utils::ConnectionId;

    // =============================================================================
    // English: Latency statistics tracker
    // 한글: 지연 시간 통계 추적기
    // =============================================================================

    struct LatencyStats
    {
        uint64_t lastRtt;       // English: Last round-trip time (ms) / 한글: 마지막 왕복 시간 (ms)
        uint64_t minRtt;        // English: Minimum RTT / 한글: 최소 RTT
        uint64_t maxRtt;        // English: Maximum RTT / 한글: 최대 RTT
        double   avgRtt;        // English: Average RTT / 한글: 평균 RTT
        uint32_t pingCount;     // English: Total pings sent / 한글: 총 핑 전송 수
        uint32_t pongCount;     // English: Total pongs received / 한글: 총 퐁 수신 수

        LatencyStats();
        void Update(uint64_t rtt);
        void Reset();
    };

    // =============================================================================
    // English: Connection state enumeration
    // 한글: 연결 상태 열거형
    // =============================================================================

    enum class ClientState : uint8_t
    {
        Disconnected = 0,       // English: Not connected / 한글: 연결 안됨
        Connecting,             // English: TCP connect in progress / 한글: TCP 연결 진행 중
        Connected,              // English: TCP connected, handshake pending / 한글: TCP 연결됨, 핸드셰이크 대기
        SessionActive,          // English: Session established / 한글: 세션 수립됨
        Disconnecting,          // English: Graceful shutdown in progress / 한글: 정상 종료 진행 중
    };

    // =============================================================================
    // English: TestClient class - synchronous Winsock2 TCP client
    // 한글: TestClient 클래스 - 동기 Winsock2 TCP 클라이언트
    // =============================================================================

    class TestClient
    {
    public:
        TestClient();
        ~TestClient();

        // English: Non-copyable
        // 한글: 복사 불가
        TestClient(const TestClient&) = delete;
        TestClient& operator=(const TestClient&) = delete;

        // =====================================================================
        // English: Lifecycle methods
        // 한글: 생명주기 메서드
        // =====================================================================

        // English: Initialize Winsock
        // 한글: Winsock 초기화
        bool Initialize();

        // English: Connect to server (blocking TCP + handshake)
        // 한글: 서버에 접속 (블로킹 TCP + 핸드셰이크)
        bool Connect(const std::string& host, uint16_t port);

        // English: Start network worker thread
        // 한글: 네트워크 워커 스레드 시작
        bool Start();

        // English: Graceful disconnect
        // 한글: 정상 연결 해제
        void Disconnect();

        // English: Full cleanup (Disconnect + WSACleanup)
        // 한글: 전체 정리 (Disconnect + WSACleanup)
        void Shutdown();

        // =====================================================================
        // English: State queries
        // 한글: 상태 조회
        // =====================================================================

        ClientState GetState() const;
        bool IsConnected() const;
        uint64_t GetSessionId() const;
        LatencyStats GetLatencyStats() const;

        // English: Request stop (called from signal handler)
        // 한글: 중지 요청 (시그널 핸들러에서 호출)
        void RequestStop();
        bool IsStopRequested() const;

    private:
        // =====================================================================
        // English: Internal methods
        // 한글: 내부 메서드
        // =====================================================================

        // English: Network worker thread function
        // 한글: 네트워크 워커 스레드 함수
        void NetworkWorkerThread();

        // English: Send raw bytes (blocking, handles partial send)
        // 한글: 원시 바이트 전송 (블로킹, 부분 전송 처리)
        bool SendRaw(const char* data, int size);

        // English: Send a typed packet struct
        // 한글: 타입된 패킷 구조체 전송
        template<typename T>
        bool SendPacket(const T& packet)
        {
            return SendRaw(reinterpret_cast<const char*>(&packet), packet.header.size);
        }

        // English: Try to read one complete packet from socket
        // 한글: 소켓에서 완전한 패킷 하나 읽기 시도
        bool RecvPacket(Core::PacketHeader& outHeader, char* outBody, int bodyBufferSize);

        // English: Packet dispatch
        // 한글: 패킷 디스패치
        void ProcessPacket(const Core::PacketHeader& header, const char* body);
        void HandleConnectResponse(const Core::PKT_SessionConnectRes* packet);
        void HandlePongResponse(const Core::PKT_PongRes* packet);

        // English: Send a ping request
        // 한글: 핑 요청 전송
        void SendPing();

    private:
        SOCKET                      mSocket;
        std::atomic<ClientState>    mState;
        std::atomic<bool>           mStopRequested;
        bool                        mWsaInitialized;

        uint64_t                    mSessionId;
        uint32_t                    mPingSequence;

        std::thread                 mWorkerThread;
        mutable std::mutex          mStatsMutex;
        LatencyStats                mStats;

        std::string                 mHost;
        uint16_t                    mPort;

        // English: Recv buffer for TCP stream reassembly
        // 한글: TCP 스트림 재조립용 수신 버퍼
        char                        mRecvBuffer[Core::RECV_BUFFER_SIZE];
        int                         mRecvBufferOffset;
    };

} // namespace Network::TestClient
