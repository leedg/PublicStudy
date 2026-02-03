#pragma once

// English: TestServer main header - game server using IOCPNetworkEngine
// 한글: TestServer 메인 헤더 - IOCPNetworkEngine 기반 게임 서버

// English: DB support can be disabled if needed
// 한글: 필요 시 DB 지원을 비활성화할 수 있음
#ifdef ENABLE_DATABASE_SUPPORT
#include "Database/DBConnectionPool.h"
#endif

#include "Network/Core/IOCPNetworkEngine.h"
#include "Network/Core/SessionManager.h"
#include "Utils/NetworkUtils.h"
#include "TestDbServerMessageHandler.h"
#include <atomic>
#include <memory>
#include <string>

namespace Network::TestServer
{
// =============================================================================
// English: TestServer class
// 한글: TestServer 클래스
// =============================================================================

class TestServer
{
  public:
	TestServer();
	~TestServer();

	// English: Lifecycle
	// 한글: 생명주기
	bool Initialize(uint16_t port = 9000,
					const std::string &dbConnectionString = "");
	bool Start();
	void Stop();
	bool IsRunning() const;

	// English: DB server integration hooks
	// 한글: DB 서버 연동용 훅
	void BindDbServerConnection(Network::Protocols::ConnectionId connectionId);
	void OnDbServerDataReceived(Network::Protocols::ConnectionId connectionId,
								const uint8_t *data, size_t size);
	void SendPingToDbServer(const std::string &message = "ping");

  private:
	// English: Network event handlers (client side)
	// 한글: 클라이언트 네트워크 이벤트 처리
	void OnConnectionEstablished(const Core::NetworkEventData &eventData);
	void OnConnectionClosed(const Core::NetworkEventData &eventData);
	void OnDataReceived(const Core::NetworkEventData &eventData);

	// English: Session factory
	// 한글: 세션 팩토리
	static Core::SessionRef CreateGameSession();

  private:
	std::unique_ptr<Core::IOCPNetworkEngine> mEngine;
	std::atomic<bool> mIsRunning;
	uint16_t mPort;
	std::string mDbConnectionString;

	// 한글: TestDBServer 전용 메시지 핸들러
	TestDbServerMessageHandler mDbServerHandler;
	Network::Protocols::ConnectionId mDbServerConnectionId;
};

} // namespace Network::TestServer
