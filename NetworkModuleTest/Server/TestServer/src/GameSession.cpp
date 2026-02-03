// English: GameSession implementation
// 한글: GameSession 구현

#include "../include/GameSession.h"
#include <chrono>
#include <ctime>
#include <iostream>
#include <sstream>

using namespace Network::Core;
using namespace Network::Utils;

#ifdef ENABLE_DATABASE_SUPPORT
using namespace Network::Database;
#endif

namespace Network::TestServer
{
GameSession::GameSession() : mConnectionRecorded(false) {}

GameSession::~GameSession() = default;

void GameSession::OnConnected()
{
	Logger::Info("GameSession connected - ID: " + std::to_string(GetId()));

	// 한글: 최초 접속 시 DB 접속 시간을 기록한다.
	if (!mConnectionRecorded)
	{
		RecordConnectTimeToDB();
		mConnectionRecorded = true;
	}
}

void GameSession::OnDisconnected()
{
	Logger::Info("GameSession disconnected - ID: " + std::to_string(GetId()));
}

void GameSession::OnRecv(const char *data, uint32_t size)
{
	if (size < sizeof(PacketHeader))
	{
		Logger::Warn("Packet too small - size: " + std::to_string(size));
		return;
	}

	const PacketHeader *header = reinterpret_cast<const PacketHeader *>(data);

	if (header->size > size)
	{
		Logger::Warn(
			"Incomplete packet - expected: " + std::to_string(header->size) +
			", received: " + std::to_string(size));
		return;
	}

	ProcessPacket(header, data, size);
}

void GameSession::ProcessPacket(const PacketHeader *header, const char *data,
								uint32_t size)
{
	// 한글: 클라이언트 패킷 처리를 전용 핸들러로 위임한다.
	mClientPacketHandler.HandlePacket(*this, header, data, size);
}

void GameSession::RecordConnectTimeToDB()
{
#ifdef ENABLE_DATABASE_SUPPORT
	ConnectionId sessionId = GetId();

	// English: Get current time string
	// 한글: 현재 시간 문자열 조회
	auto now = std::chrono::system_clock::now();
	auto time = std::chrono::system_clock::to_time_t(now);

	std::tm localTime;
#ifdef _WIN32
	localtime_s(&localTime, &time);
#else
	localtime_r(&time, &localTime);
#endif

	char timeStr[64];
	std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &localTime);

	// English: Execute DB query via connection pool
	// 한글: 접속 풀을 통해 DB 쿼리 실행
	if (!DBConnectionPool::Instance().IsInitialized())
	{
		Logger::Info("DB not initialized - skipping connect time recording for "
					 "Session: " +
					 std::to_string(sessionId));
		return;
	}

	ScopedDBConnection dbConn;

	if (dbConn.IsValid())
	{
		std::ostringstream query;
		query << "INSERT INTO SessionLog (SessionId, ConnectTime) VALUES ("
				  << sessionId << ", '" << timeStr << "')";

		if (dbConn->Execute(query.str()))
		{
			Logger::Info("Connect time recorded - Session: " +
						 std::to_string(sessionId));
		}
		else
		{
			Logger::Error("Failed to record connect time - Session: " +
						  std::to_string(sessionId) + " - " +
						  dbConn->GetLastError());
		}
	}
	else
	{
		Logger::Warn("No DB connection available for Session: " +
					 std::to_string(sessionId));
	}
#else
	Logger::Info("Database support disabled - skipping connect time recording");
#endif
}

} // namespace Network::TestServer
