#pragma once

// English: PacketStream - TCP stream reassembly and packet send/recv
// 한글: PacketStream - TCP 스트림 재조립 및 패킷 송수신

#include "PlatformSocket.h"
#include "Network/Core/PacketDefine.h"

namespace Network::TestClient
{

// =============================================================================
// English: Result of a packet receive operation
// 한글: 패킷 수신 작업의 결과
// =============================================================================

enum class RecvResult : uint8_t
{
	Success,          // English: Complete packet received / 한글: 완전한 패킷 수신됨
	Timeout,          // English: Timeout or would-block / 한글: 타임아웃 또는 대기
	ConnectionClosed, // English: Server closed connection / 한글: 서버가 연결 종료
	Error,            // English: Socket error / 한글: 소켓 에러
	InvalidPacket,    // English: Malformed packet / 한글: 잘못된 패킷
};

// =============================================================================
// English: PacketStream - handles TCP stream reassembly and raw send/recv
// 한글: PacketStream - TCP 스트림 재조립 및 원시 송수신 처리
//
// This class owns the recv buffer but does NOT own the socket.
// Call Attach() to bind a connected socket, Reset() on disconnect.
// =============================================================================

class PacketStream
{
  public:
	PacketStream();

	// English: Attach to a connected socket (non-owning reference)
	// 한글: 연결된 소켓에 연결 (비소유 참조)
	void Attach(SocketHandle socket);

	// English: Reset buffer state (call on disconnect/reconnect)
	// 한글: 버퍼 상태 초기화 (연결 해제/재연결 시 호출)
	void Reset();

	// English: Try to read one complete packet from the socket
	// 한글: 소켓에서 완전한 패킷 하나 읽기 시도
	RecvResult RecvPacket(Core::PacketHeader &outHeader, char *outBody,
						  int bodyBufferSize);

	// English: Send raw bytes (blocking, handles partial send)
	// 한글: 원시 바이트 전송 (블로킹, 부분 전송 처리)
	bool SendRaw(const char *data, int size);

	// English: Send a typed packet struct
	// 한글: 타입된 패킷 구조체 전송
	template <typename T> bool SendPacket(const T &packet)
	{
		return SendRaw(reinterpret_cast<const char *>(&packet),
					   packet.header.size);
	}

  private:
	SocketHandle mSocket;
	char mRecvBuffer[Core::RECV_BUFFER_SIZE];
	int mRecvBufferOffset;
};

} // namespace Network::TestClient
