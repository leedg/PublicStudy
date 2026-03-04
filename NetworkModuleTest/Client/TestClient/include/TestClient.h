#pragma once

// English: TestClient - synchronous TCP client for testing game server
// 한글: TestClient - 게임 서버 테스트용 동기 TCP 클라이언트

#include "LatencyStats.h"
#include "PacketStream.h"
#include "PlatformSocket.h"
#include "Network/Core/PacketDefine.h"
#include "Utils/NetworkUtils.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace Network::TestClient
{
using Utils::ConnectionId;

// =============================================================================
// English: TestClient class - synchronous cross-platform TCP client
// 한글: TestClient 클래스 - 동기 크로스 플랫폼 TCP 클라이언트
// =============================================================================

class TestClient
{
  public:
	TestClient();
	~TestClient();

	// English: Non-copyable
	// 한글: 복사 불가
	TestClient(const TestClient &) = delete;
	TestClient &operator=(const TestClient &) = delete;

	// =====================================================================
	// English: Lifecycle methods
	// 한글: 생명주기 메서드
	// =====================================================================

	// English: Initialize socket platform
	// 한글: 소켓 플랫폼 초기화
	bool Initialize();

	// English: Connect to server (blocking TCP + handshake)
	// 한글: 서버에 접속 (블로킹 TCP + 핸드셰이크)
	bool Connect(const std::string &host, uint16_t port);

	// English: Start network worker thread
	// 한글: 네트워크 워커 스레드 시작
	bool Start();

	// English: Graceful disconnect
	// 한글: 정상 연결 해제
	void Disconnect();

	// English: Full cleanup (Disconnect + platform cleanup)
	// 한글: 전체 정리 (Disconnect + 플랫폼 정리)
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

	// English: Set maximum ping count; client stops after sending this many pings (0 = unlimited)
	// 한글: 최대 핑 횟수 설정; 이 횟수만큼 핑을 보낸 후 종료 (0 = 무제한)
	void SetMaxPings(uint32_t maxPings);

  private:
	// =====================================================================
	// English: Internal methods
	// 한글: 내부 메서드
	// =====================================================================

	// English: Network worker thread function
	// 한글: 네트워크 워커 스레드 함수
	void NetworkWorkerThread();

	// English: Packet dispatch
	// 한글: 패킷 디스패치
	void ProcessPacket(const Core::PacketHeader &header, const char *body);
	void HandleConnectResponse(const Core::PKT_SessionConnectRes *packet);
	void HandlePongResponse(const Core::PKT_PongRes *packet);

	// English: Send a ping request
	// 한글: 핑 요청 전송
	void SendPing();

  private:
	SocketHandle mSocket;
	std::atomic<ClientState> mState;
	std::atomic<bool> mStopRequested;
	bool mPlatformInitialized;

	uint64_t mSessionId;
	uint32_t mPingSequence;
	uint32_t mMaxPings;

	std::thread mWorkerThread;
	mutable std::mutex mStatsMutex;
	LatencyStats mStats;

	std::string mHost;
	uint16_t mPort;

	// English: Packet stream handler for TCP send/recv
	// 한글: TCP 송수신을 위한 패킷 스트림 핸들러
	PacketStream mStream;
};

} // namespace Network::TestClient
