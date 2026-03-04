// English: PacketStream implementation - TCP stream reassembly and raw send
// 한글: PacketStream 구현 - TCP 스트림 재조립 및 원시 송신

#include "../include/PacketStream.h"
#include "Utils/Logger.h"
#include <cstring>

using namespace Network::Core;

namespace Network::TestClient
{

PacketStream::PacketStream()
	: mSocket(INVALID_SOCKET_HANDLE), mRecvBufferOffset(0)
{
	std::memset(mRecvBuffer, 0, sizeof(mRecvBuffer));
}

void PacketStream::Attach(SocketHandle socket) { mSocket = socket; }

void PacketStream::Reset()
{
	mSocket = INVALID_SOCKET_HANDLE;
	mRecvBufferOffset = 0;
	std::memset(mRecvBuffer, 0, sizeof(mRecvBuffer));
}

// =========================================================================
// English: Recv one complete packet with TCP stream reassembly
// 한글: TCP 스트림 재조립으로 완전한 패킷 하나 수신
// =========================================================================

RecvResult PacketStream::RecvPacket(PacketHeader &outHeader, char *outBody,
									int bodyBufferSize)
{
	// English: Try to receive more data into buffer
	// 한글: 버퍼에 더 많은 데이터 수신 시도
	if (mRecvBufferOffset < static_cast<int>(sizeof(PacketHeader)))
	{
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
			Utils::Logger::Info("Server closed connection");
			return RecvResult::ConnectionClosed;
		}
		else
		{
			int err = PlatformGetLastError();
			if (IsTimeoutOrWouldBlock(err))
			{
				return RecvResult::Timeout;
			}
			Utils::Logger::Error("recv() failed: " + std::to_string(err));
			return RecvResult::Error;
		}
	}

	// English: Check if we have a complete header
	// 한글: 완전한 헤더가 있는지 확인
	if (mRecvBufferOffset < static_cast<int>(sizeof(PacketHeader)))
	{
		return RecvResult::Timeout;
	}

	// English: Read packet size from header
	// 한글: 헤더에서 패킷 크기 읽기
	const PacketHeader *pHeader =
		reinterpret_cast<const PacketHeader *>(mRecvBuffer);
	int packetSize = pHeader->size;

	if (packetSize < static_cast<int>(sizeof(PacketHeader)) ||
		packetSize > MAX_PACKET_SIZE)
	{
		Utils::Logger::Error("Invalid packet size: " +
							 std::to_string(packetSize));
		return RecvResult::InvalidPacket;
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
			Utils::Logger::Info("Server closed connection");
			return RecvResult::ConnectionClosed;
		}
		else
		{
			int err = PlatformGetLastError();
			if (IsTimeoutOrWouldBlock(err))
			{
				return RecvResult::Timeout;
			}
			Utils::Logger::Error("recv() failed: " + std::to_string(err));
			return RecvResult::Error;
		}
	}

	// English: Complete packet received - copy out
	// 한글: 완전한 패킷 수신됨 - 복사
	outHeader = *pHeader;
	// [Fix A-3] bodySize 가 음수인 경우는 위에서 packetSize >= sizeof(PacketHeader)를
	// 이미 보장하지만, bodySize > bodyBufferSize 란 조용한 데이터 손실을
	// 에러로 명시적으로 처리한다.
	int bodySize = packetSize - static_cast<int>(sizeof(PacketHeader));
	if (bodySize > bodyBufferSize)
	{
		Utils::Logger::Error("Body buffer too small: need " + std::to_string(bodySize) +
		                     " but got " + std::to_string(bodyBufferSize));
		return RecvResult::InvalidPacket;
	}
	if (bodySize > 0)
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

	return RecvResult::Success;
}

// =========================================================================
// English: Send raw bytes (blocking, handles partial send)
// 한글: 원시 바이트 전송 (블로킹, 부분 전송 처리)
// =========================================================================

bool PacketStream::SendRaw(const char *data, int size)
{
	int totalSent = 0;
	while (totalSent < size)
	{
		int sent = send(mSocket, data + totalSent, size - totalSent, 0);
		if (sent == SOCKET_ERROR_VALUE)
		{
			Utils::Logger::Error("send() failed: " +
								 std::to_string(PlatformGetLastError()));
			return false;
		}
		totalSent += sent;
	}
	return true;
}

} // namespace Network::TestClient
