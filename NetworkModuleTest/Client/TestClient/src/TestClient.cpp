// English: TestClient implementation - orchestration and protocol handling
// 한글: TestClient 구현 - 오케스트레이션 및 프로토콜 처리

#include "../include/TestClient.h"
#include "Utils/PingPongConfig.h"
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

#ifdef _MSC_VER
#pragma comment(lib, "Ws2_32.lib")
#endif

using namespace Network::Core;
using namespace Network::Utils;

namespace Network::TestClient
{

// =============================================================================
// English: TestClient implementation
// 한글: TestClient 구현
// =============================================================================

TestClient::TestClient()
	: mSocket(INVALID_SOCKET_HANDLE), mState(ClientState::Disconnected),
		  mStopRequested(false), mPlatformInitialized(false), mSessionId(0),
		  mPingSequence(0), mMaxPings(0), mPort(0)
{
}

void TestClient::SetMaxPings(uint32_t maxPings)
{
	mMaxPings = maxPings;
}

TestClient::~TestClient() { Shutdown(); }

// =========================================================================
// English: Initialize socket platform
// 한글: 소켓 플랫폼 초기화
// =========================================================================

bool TestClient::Initialize()
{
	if (!PlatformSocketInit())
	{
		Logger::Error("Socket platform initialization failed");
		return false;
	}

	mPlatformInitialized = true;
	Logger::Info("Socket platform initialized");
	return true;
}

// =========================================================================
// English: Connect to server
// 한글: 서버에 접속
// =========================================================================

bool TestClient::Connect(const std::string &host, uint16_t port)
{
	if (mState.load() != ClientState::Disconnected)
	{
		Logger::Warn("Already connected or connecting");
		return false;
	}

	// English: Reset stop flag so reconnect works after a previous session ended
	// 한글: 이전 세션 종료 후 재연결이 동작하도록 stop 플래그 리셋
	mStopRequested.store(false);

	mHost = host;
	mPort = port;
	mState.store(ClientState::Connecting);

	// English: Resolve server address
	// 한글: 서버 주소 해석
	struct addrinfo hints = {};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	std::string portStr = std::to_string(port);
	struct addrinfo *addrResult = nullptr;

	int ret = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &addrResult);
	if (ret != 0)
	{
		Logger::Error("getaddrinfo failed: " + std::to_string(ret));
		if (addrResult)
			freeaddrinfo(addrResult);
		mState.store(ClientState::Disconnected);
		return false;
	}

	// English: Create socket
	// 한글: 소켓 생성
	mSocket = socket(addrResult->ai_family, addrResult->ai_socktype,
					 addrResult->ai_protocol);
	if (mSocket == INVALID_SOCKET_HANDLE)
	{
		Logger::Error("socket() failed: " + std::to_string(PlatformGetLastError()));
		freeaddrinfo(addrResult);
		mStream.Reset();
		mState.store(ClientState::Disconnected);
		return false;
	}

	// English: TCP connect (blocking)
	// 한글: TCP 접속 (블로킹)
	Logger::Info("Connecting to " + host + ":" + std::to_string(port) + "...");
	ret = connect(mSocket, addrResult->ai_addr,
				  static_cast<int>(addrResult->ai_addrlen));
	freeaddrinfo(addrResult);

	if (ret == SOCKET_ERROR_VALUE)
	{
		Logger::Error("connect() failed: " + std::to_string(PlatformGetLastError()));
		PlatformCloseSocket(mSocket);
		mSocket = INVALID_SOCKET_HANDLE;
		mStream.Reset();
		mState.store(ClientState::Disconnected);
		return false;
	}

	// English: Set socket options
	// 한글: 소켓 옵션 설정
	PlatformSetTcpNoDelay(mSocket, true);

	// English: Set recv timeout (1 second) for non-blocking worker loop
	// 한글: 논블로킹 워커 루프를 위한 수신 타임아웃 (1초) 설정
	PlatformSetRecvTimeout(mSocket, 1000);

	mState.store(ClientState::Connected);
	Logger::Info("TCP connected");

	// English: Attach socket to packet stream
	// 한글: 패킷 스트림에 소켓 연결
	mStream.Attach(mSocket);

	// English: Send SessionConnectReq
	// 한글: SessionConnectReq 전송
	PKT_SessionConnectReq connectReq;
	connectReq.clientVersion = 1;

	if (!mStream.SendPacket(connectReq))
	{
		Logger::Error("Failed to send SessionConnectReq");
		// English: Clean up socket and stream consistently
		// 한글: 소켓과 스트림 정리 통일
		mStream.Reset();
		PlatformCloseSocket(mSocket);
		mSocket = INVALID_SOCKET_HANDLE;
		mState.store(ClientState::Disconnected);
		return false;
	}

	Logger::Info("SessionConnectReq sent, waiting for response...");

	// English: Wait for SessionConnectRes (blocking recv)
	// 한글: SessionConnectRes 대기 (블로킹 수신)
	char responseBuffer[sizeof(PKT_SessionConnectRes)];
	int totalReceived = 0;
	int expectedSize = sizeof(PKT_SessionConnectRes);

	while (totalReceived < expectedSize)
	{
		int received = recv(mSocket, responseBuffer + totalReceived,
							expectedSize - totalReceived, 0);
		if (received <= 0)
		{
			Logger::Error("Failed to receive SessionConnectRes: " +
						  std::to_string(PlatformGetLastError()));
			PlatformCloseSocket(mSocket);
			mSocket = INVALID_SOCKET_HANDLE;
			mStream.Reset();
			mState.store(ClientState::Disconnected);
			return false;
		}
		totalReceived += received;
	}

	const PKT_SessionConnectRes *response =
		reinterpret_cast<const PKT_SessionConnectRes *>(responseBuffer);

	ConnectResult connResult = static_cast<ConnectResult>(response->result);

	if (connResult != ConnectResult::Success)
	{
		Logger::Error("Connection rejected - result: " +
						  std::to_string(response->result));
		PlatformCloseSocket(mSocket);
		mSocket = INVALID_SOCKET_HANDLE;
		mStream.Reset();
		mState.store(ClientState::Disconnected);
		return false;
	}

	mSessionId = response->sessionId;
	mState.store(ClientState::SessionActive);

	Logger::Info("Session established - ID: " + std::to_string(mSessionId) +
				 ", ServerTime: " + std::to_string(response->serverTime));

	return true;
}

// =========================================================================
// English: Start worker thread
// 한글: 워커 스레드 시작
// =========================================================================

bool TestClient::Start()
{
	if (mState.load() != ClientState::SessionActive)
	{
		Logger::Error("Cannot start - session not active");
		return false;
	}

	mStopRequested.store(false);
	mWorkerThread = std::thread(&TestClient::NetworkWorkerThread, this);

	Logger::Info("Network worker thread started");
	return true;
}

// =========================================================================
// English: Network worker thread
// 한글: 네트워크 워커 스레드
// =========================================================================

void TestClient::NetworkWorkerThread()
{
	Logger::Debug("Worker thread entered");

	auto lastPingTime = Timer::GetCurrentTimestamp();

	while (!mStopRequested.load() &&
			   mState.load() == ClientState::SessionActive)
	{
		// English: Check if it's time to send a ping
		// 한글: 핑 전송 시간인지 확인
		auto now = Timer::GetCurrentTimestamp();
		if (now - lastPingTime >= PING_INTERVAL_MS)
		{
			if (mMaxPings > 0 && mPingSequence >= mMaxPings)
			{
				mStopRequested.store(true);
				break;
			}
			SendPing();
			lastPingTime = now;
		}

		// English: Try to receive a packet (non-blocking due to SO_RCVTIMEO)
		// 한글: 패킷 수신 시도 (SO_RCVTIMEO로 인해 논블로킹)
		PacketHeader header;
		char body[MAX_PACKET_SIZE];

		RecvResult result = mStream.RecvPacket(header, body, sizeof(body));

		if (result == RecvResult::Success)
		{
			ProcessPacket(header, body);
		}
		else if (result == RecvResult::ConnectionClosed ||
				 result == RecvResult::Error ||
				 result == RecvResult::InvalidPacket)
		{
			// English: Fatal recv error - mark disconnected
			// 한글: 치명적 수신 에러 - 연결 해제로 표시
			mState.store(ClientState::Disconnected);
		}
		// English: RecvResult::Timeout - normal, try again next iteration
		// 한글: RecvResult::Timeout - 정상, 다음 반복에서 재시도
	}

	Logger::Debug("Worker thread exiting");
}

// =========================================================================
// English: Process received packet
// 한글: 수신된 패킷 처리
// =========================================================================

void TestClient::ProcessPacket(const PacketHeader &header, const char *body)
{
	PacketType packetType = static_cast<PacketType>(header.id);

	switch (packetType)
	{
	case PacketType::SessionConnectRes:
	{
		// English: Validate packet size before reconstructing
		// 한글: 재구성 전에 패킷 크기 검증
		if (header.size < sizeof(PKT_SessionConnectRes))
		{
			Logger::Error("SessionConnectRes: packet too small (" +
						  std::to_string(header.size) + " < " +
						  std::to_string(sizeof(PKT_SessionConnectRes)) + ")");
			break;
		}
		char fullPacket[sizeof(PKT_SessionConnectRes)];
		std::memcpy(fullPacket, &header, sizeof(PacketHeader));
		std::memcpy(fullPacket + sizeof(PacketHeader), body,
					sizeof(PKT_SessionConnectRes) - sizeof(PacketHeader));
		HandleConnectResponse(
			reinterpret_cast<const PKT_SessionConnectRes *>(fullPacket));
		break;
	}
	case PacketType::PongRes:
	{
		// English: Validate packet size before reconstructing
		// 한글: 재구성 전에 패킷 크기 검증
		if (header.size < sizeof(PKT_PongRes))
		{
			Logger::Error("PongRes: packet too small (" +
						  std::to_string(header.size) + " < " +
						  std::to_string(sizeof(PKT_PongRes)) + ")");
			break;
		}
		char fullPacket[sizeof(PKT_PongRes)];
		std::memcpy(fullPacket, &header, sizeof(PacketHeader));
		std::memcpy(fullPacket + sizeof(PacketHeader), body,
					sizeof(PKT_PongRes) - sizeof(PacketHeader));
		HandlePongResponse(reinterpret_cast<const PKT_PongRes *>(fullPacket));
		break;
	}
	default:
		{
			// 한글: 패킷 ID를 16진수 4자리로 포맷한다.
			std::ostringstream oss;
			oss << std::uppercase << std::hex << std::setw(4)
				<< std::setfill('0') << header.id;
			Logger::Warn("Unknown packet type: 0x" + oss.str());
		}
		break;
	}
}

// =========================================================================
// English: Handle SessionConnectRes
// 한글: SessionConnectRes 처리
// =========================================================================

void TestClient::HandleConnectResponse(const PKT_SessionConnectRes *packet)
{
	Logger::Info("Received additional ConnectRes - SessionId: " +
				 std::to_string(packet->sessionId));
}

// =========================================================================
// English: Handle PongRes - calculate RTT
// 한글: PongRes 처리 - RTT 계산
// =========================================================================

void TestClient::HandlePongResponse(const PKT_PongRes *packet)
{
	uint64_t now = Timer::GetCurrentTimestamp();
	
	// English: Guard against clock skew / time going backwards
	// 한글: 시계 역행 방어
	if (now < packet->clientTime)
	{
		Logger::Warn("HandlePongResponse: System clock skew detected - skipping RTT update");
		return;
	}
	
	uint64_t rtt = now - packet->clientTime;

	{
		std::lock_guard<std::mutex> lock(mStatsMutex);
		mStats.Update(rtt);
	}

#ifdef ENABLE_PINGPONG_VERBOSE_LOG
	Logger::Debug("Pong received - Seq: " + std::to_string(packet->sequence) +
				  ", RTT: " + std::to_string(rtt) + "ms" +
				  ", ServerTime: " + std::to_string(packet->serverTime));
#else
	if (packet->sequence % PINGPONG_LOG_INTERVAL == 0)
	{
		Logger::Info("[Client] Pong received (every " + std::to_string(PINGPONG_LOG_INTERVAL) + "th) - Seq: " +
					 std::to_string(packet->sequence) + ", RTT: " + std::to_string(rtt) + "ms");
	}
#endif
}

// =========================================================================
// English: Send ping request
// 한글: 핑 요청 전송
// =========================================================================

void TestClient::SendPing()
{
	PKT_PingReq pingReq;
	pingReq.clientTime = Timer::GetCurrentTimestamp();
	pingReq.sequence = mPingSequence++;

	if (mStream.SendPacket(pingReq))
	{
		std::lock_guard<std::mutex> lock(mStatsMutex);
		mStats.pingCount++;

#ifdef ENABLE_PINGPONG_VERBOSE_LOG
		Logger::Debug("[Client] Ping sent - Seq: " + std::to_string(pingReq.sequence));
#else
		if (pingReq.sequence % PINGPONG_LOG_INTERVAL == 0)
		{
			Logger::Info("[Client] Ping sent (every " + std::to_string(PINGPONG_LOG_INTERVAL) + "th) - Seq: " +
						 std::to_string(pingReq.sequence) + ", Total: " + std::to_string(mStats.pingCount));
		}
#endif
	}
	else
	{
		Logger::Warn("Failed to send PingReq");
	}
}

// =========================================================================
// English: Disconnect
// 한글: 연결 해제
// =========================================================================

void TestClient::Disconnect()
{
	if (mState.load() == ClientState::Disconnected)
	{
		return;
	}

	mState.store(ClientState::Disconnecting);
	mStopRequested.store(true);

	// English: Wait for worker thread to finish
	// 한글: 워커 스레드 종료 대기
	if (mWorkerThread.joinable())
	{
		mWorkerThread.join();
	}

	// English: Close socket
	// 한글: 소켓 닫기
	if (mSocket != INVALID_SOCKET_HANDLE)
	{
		shutdown(mSocket, SHUT_RDWR_VALUE);
		PlatformCloseSocket(mSocket);
		mSocket = INVALID_SOCKET_HANDLE;
	}

	// English: Reset packet stream state
	// 한글: 패킷 스트림 상태 초기화
	mStream.Reset();

	mState.store(ClientState::Disconnected);
	Logger::Info("Disconnected");
}

// =========================================================================
// English: Shutdown (full cleanup)
// 한글: 종료 (전체 정리)
// =========================================================================

void TestClient::Shutdown()
{
	Disconnect();

	if (mPlatformInitialized)
	{
		PlatformSocketCleanup();
		mPlatformInitialized = false;
	}
}

// =========================================================================
// English: State accessors
// 한글: 상태 접근자
// =========================================================================

ClientState TestClient::GetState() const { return mState.load(); }

bool TestClient::IsConnected() const
{
	ClientState state = mState.load();
	return state == ClientState::Connected ||
			   state == ClientState::SessionActive;
}

uint64_t TestClient::GetSessionId() const { return mSessionId; }

LatencyStats TestClient::GetLatencyStats() const
{
	std::lock_guard<std::mutex> lock(mStatsMutex);
	return mStats;
}

void TestClient::RequestStop() { mStopRequested.store(true); }

bool TestClient::IsStopRequested() const { return mStopRequested.load(); }

} // namespace Network::TestClient
