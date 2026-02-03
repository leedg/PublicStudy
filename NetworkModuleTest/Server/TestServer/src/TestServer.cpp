// English: TestServer implementation
// 한글: TestServer 구현

#include "../include/TestServer.h"
#include "../include/GameSession.h"
#include <iostream>

using namespace Network::Core;
using namespace Network::Utils;

#ifdef ENABLE_DATABASE_SUPPORT
using namespace Network::Database;
#endif

namespace Network::TestServer
{
TestServer::TestServer()
	: mIsRunning(false), mPort(0), mDbServerConnectionId(0)
{
}

TestServer::~TestServer()
{
	if (mIsRunning.load())
	{
		Stop();
	}
}

bool TestServer::Initialize(uint16_t port,
							const std::string &dbConnectionString)
{
	// English: Initialize DB connection pool (optional)
	// 한글: DB 연결 풀 초기화 (선택 사항)
#ifdef ENABLE_DATABASE_SUPPORT
	if (!dbConnectionString.empty())
	{
		if (!DBConnectionPool::Instance().Initialize(dbConnectionString, 5))
		{
			Logger::Warn(
				"Failed to initialize DB pool (continuing without DB)");
		}
	}
	else
	{
		Logger::Info("No DB connection string - running without DB");
	}
#else
	(void)dbConnectionString; // Suppress unused warning
	Logger::Info("Database support disabled at compile time");
#endif

	// English: Create and initialize network engine
	// 한글: 네트워크 엔진 생성 및 초기화
	mEngine =
		std::unique_ptr<Core::IOCPNetworkEngine>(new Core::IOCPNetworkEngine());

	if (!mEngine->Initialize(MAX_CONNECTIONS, port))
	{
		Logger::Error("Failed to initialize network engine");
		return false;
	}

	// English: Register event callbacks
	// 한글: 이벤트 콜백 등록
	mEngine->RegisterEventCallback(NetworkEvent::Connected,
									   [this](const NetworkEventData &e)
									   { OnConnectionEstablished(e); });

	mEngine->RegisterEventCallback(NetworkEvent::Disconnected,
									   [this](const NetworkEventData &e)
									   { OnConnectionClosed(e); });

	mEngine->RegisterEventCallback(NetworkEvent::DataReceived,
									   [this](const NetworkEventData &e)
									   { OnDataReceived(e); });

	// 한글: DB 서버 메시지 핸들러 초기화 및 전송 콜백 연결
	mDbServerHandler.Initialize();
	mDbServerHandler.SetSendCallback(
		[this](Network::Protocols::ConnectionId connectionId,
			   const std::vector<uint8_t> &data)
		{
			(void)connectionId;
			// 한글: 실제 전송은 추후 네트워크 모듈과 연결한다.
			Logger::Debug("DBServer send requested - bytes: " +
						  std::to_string(data.size()));
		});

	Logger::Info("TestServer initialized on port " + std::to_string(port));
	return true;
}

bool TestServer::Start()
{
	if (!mEngine)
	{
		Logger::Error("TestServer not initialized");
		return false;
	}

	if (!mEngine->Start())
	{
		Logger::Error("Failed to start network engine");
		return false;
	}

	mIsRunning.store(true);
	Logger::Info("TestServer started");
	return true;
}

void TestServer::Stop()
{
	if (!mIsRunning.load())
	{
		return;
	}

	mIsRunning.store(false);

	if (mEngine)
	{
		mEngine->Stop();
	}

	// English: Shutdown DB pool
	// 한글: DB 풀 종료
#ifdef ENABLE_DATABASE_SUPPORT
	if (DBConnectionPool::Instance().IsInitialized())
	{
		DBConnectionPool::Instance().Shutdown();
	}
#endif

	Logger::Info("TestServer stopped");
}

bool TestServer::IsRunning() const { return mIsRunning.load(); }

void TestServer::BindDbServerConnection(
	Network::Protocols::ConnectionId connectionId)
{
	// 한글: DB 서버 연결 ID를 저장해 이후 Ping 전송에 사용한다.
	mDbServerConnectionId = connectionId;
	Logger::Info("DBServer connection bound - ID: " +
				 std::to_string(connectionId));
}

void TestServer::OnDbServerDataReceived(
	Network::Protocols::ConnectionId connectionId, const uint8_t *data,
	size_t size)
{
	// 한글: DB 서버 데이터는 전용 핸들러로 분리 처리한다.
	if (!data || size == 0)
	{
		Logger::Warn("DBServer data ignored - empty payload");
		return;
	}

	mDbServerHandler.ProcessIncomingData(connectionId, data, size);
}

void TestServer::SendPingToDbServer(const std::string &message)
{
	if (mDbServerConnectionId == 0)
	{
		Logger::Warn("DBServer connection not bound - skipping ping");
		return;
	}

	mDbServerHandler.SendPing(mDbServerConnectionId, message);
}

void TestServer::OnConnectionEstablished(const NetworkEventData &eventData)
{
	Logger::Info("Client accepted - Connection: " +
				 std::to_string(eventData.connectionId));
}

void TestServer::OnConnectionClosed(const NetworkEventData &eventData)
{
	Logger::Info("Client disconnected - Connection: " +
				 std::to_string(eventData.connectionId));
}

void TestServer::OnDataReceived(const NetworkEventData &eventData)
{
	Logger::Debug(
		"Received " + std::to_string(eventData.dataSize) +
		" bytes from Connection: " + std::to_string(eventData.connectionId));
}

SessionRef TestServer::CreateGameSession()
{
	return std::make_shared<GameSession>();
}

} // namespace Network::TestServer
