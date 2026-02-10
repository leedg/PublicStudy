// English: TestClient implementation
// 한글: TestClient 구현

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
// English: LatencyStats implementation
// 한글: LatencyStats 구현
// =============================================================================

LatencyStats::LatencyStats() { Reset(); }

void LatencyStats::Update(uint64_t rtt)
{
	lastRtt = rtt;
	if (rtt < minRtt)
		minRtt = rtt;
	if (rtt > maxRtt)
		maxRtt = rtt;

	// English: Running average calculation
	// 한글: 이동 평균 계산
	avgRtt =
		((avgRtt * pongCount) + static_cast<double>(rtt)) / (pongCount + 1);
	++pongCount;
}

void LatencyStats::Reset()
{
	lastRtt = 0;
	minRtt = UINT64_MAX;
	maxRtt = 0;
	avgRtt = 0.0;
	pingCount = 0;
	pongCount = 0;
}

// =============================================================================
// English: TestClient implementation
// 한글: TestClient 구현
// =============================================================================

TestClient::TestClient()
	: mSocket(INVALID_SOCKET_HANDLE), mState(ClientState::Disconnected),
		  mStopRequested(false), mPlatformInitialized(false), mSessionId(0),
		  mPingSequence(0), mPort(0), mRecvBufferOffset(0)
{
	std::memset(mRecvBuffer, 0, sizeof(mRecvBuffer));
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

	// English: Send SessionConnectReq
	// 한글: SessionConnectReq 전송
	PKT_SessionConnectReq connectReq;
	connectReq.clientVersion = 1;

	if (!SendPacket(connectReq))
	{
		Logger::Error("Failed to send SessionConnectReq");
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
			SendPing();
			lastPingTime = now;
		}

		// English: Try to receive a packet (non-blocking due to SO_RCVTIMEO)
		// 한글: 패킷 수신 시도 (SO_RCVTIMEO로 인해 논블로킹)
		PacketHeader header;
		char body[MAX_PACKET_SIZE];

		if (RecvPacket(header, body, sizeof(body)))
		{
			ProcessPacket(header, body);
		}
	}

	Logger::Debug("Worker thread exiting");
}

// =========================================================================
// English: Recv one complete packet with TCP stream reassembly
// 한글: TCP 스트림 재조립으로 완전한 패킷 하나 수신
// =========================================================================

bool TestClient::RecvPacket(PacketHeader &outHeader, char *outBody,
							int bodyBufferSize)
{
	// English: Try to receive more data into buffer
	// 한글: 버퍼에 더 많은 데이터 수신 시도
	if (mRecvBufferOffset < static_cast<int>(sizeof(PacketHeader)))
	{
		int bytesNeeded =
			static_cast<int>(sizeof(PacketHeader)) - mRecvBufferOffset;
		int received = recv(mSocket, mRecvBuffer + mRecvBufferOffset,
							RECV_BUFFER_SIZE - mRecvBufferOffset, 0);

		if (received > 0)
		{
			mRecvBufferOffset += received;
		}
		else if (received == 0)
		{
			// English: Connection closed by server
			// 한글: 서버에 의해 연결 종료됨
			Logger::Info("Server closed connection");
			mState.store(ClientState::Disconnected);
			return false;
		}
		else
		{
			int err = PlatformGetLastError();
			if (IsTimeoutOrWouldBlock(err))
			{
				return false; // English: Timeout, not an error / 한글:
								  // 타임아웃, 에러 아님
			}
			Logger::Error("recv() failed: " + std::to_string(err));
			mState.store(ClientState::Disconnected);
			return false;
		}
	}

	// English: Check if we have a complete header
	// 한글: 완전한 헤더가 있는지 확인
	if (mRecvBufferOffset < static_cast<int>(sizeof(PacketHeader)))
	{
		return false;
	}

	// English: Read packet size from header
	// 한글: 헤더에서 패킷 크기 읽기
	const PacketHeader *pHeader =
		reinterpret_cast<const PacketHeader *>(mRecvBuffer);
	int packetSize = pHeader->size;

	if (packetSize < static_cast<int>(sizeof(PacketHeader)) ||
		packetSize > MAX_PACKET_SIZE)
	{
		Logger::Error("Invalid packet size: " + std::to_string(packetSize));
		mState.store(ClientState::Disconnected);
		return false;
	}

	// English: Try to receive remaining bytes if needed
	// 한글: 필요한 경우 나머지 바이트 수신 시도
	while (mRecvBufferOffset < packetSize)
	{
		int received = recv(mSocket, mRecvBuffer + mRecvBufferOffset,
							packetSize - mRecvBufferOffset, 0);

		if (received > 0)
		{
			mRecvBufferOffset += received;
		}
		else if (received == 0)
		{
			Logger::Info("Server closed connection");
			mState.store(ClientState::Disconnected);
			return false;
		}
		else
		{
			int err = PlatformGetLastError();
			if (IsTimeoutOrWouldBlock(err))
			{
				return false; // English: Incomplete, try again later / 한글:
								  // 불완전, 나중에 재시도
			}
			Logger::Error("recv() failed: " + std::to_string(err));
			mState.store(ClientState::Disconnected);
			return false;
		}
	}

	// English: Complete packet received - copy out
	// 한글: 완전한 패킷 수신됨 - 복사
	outHeader = *pHeader;
	int bodySize = packetSize - sizeof(PacketHeader);
	if (bodySize > 0 && bodySize <= bodyBufferSize)
	{
		std::memcpy(outBody, mRecvBuffer + sizeof(PacketHeader), bodySize);
	}

	// English: Shift remaining data in buffer
	// 한글: 버퍼의 나머지 데이터 이동
	int remaining = mRecvBufferOffset - packetSize;
	if (remaining > 0)
	{
		std::memmove(mRecvBuffer, mRecvBuffer + packetSize, remaining);
	}
	mRecvBufferOffset = remaining;

	return true;
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
		// English: Reconstruct full packet for handler
		// 한글: 핸들러용 전체 패킷 재구성
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

	if (SendPacket(pingReq))
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
// English: Send raw bytes
// 한글: 원시 바이트 전송
// =========================================================================

bool TestClient::SendRaw(const char *data, int size)
{
	int totalSent = 0;
	while (totalSent < size)
	{
		int sent = send(mSocket, data + totalSent, size - totalSent, 0);
		if (sent == SOCKET_ERROR_VALUE)
		{
			Logger::Error("send() failed: " +
						  std::to_string(PlatformGetLastError()));
			return false;
		}
		totalSent += sent;
	}
	return true;
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
