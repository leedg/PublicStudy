#pragma once

// English: Database Server main header
// 한글: 데이터베이스 서버 메인 헤더

// English: DBPingTimeManager replaced by ServerLatencyManager (merged) — see DBPingTimeManager.h for migration notes
// 한글: DBPingTimeManager는 ServerLatencyManager로 통합됨 — 마이그레이션 노트는 DBPingTimeManager.h 참조
#include "ServerLatencyManager.h"

// English: Forward-declare IDatabase; full definition included in DBServer.cpp
// 한글: IDatabase 전방 선언; DBServer.cpp에서 전체 정의 포함
namespace Network { namespace Database { class IDatabase; } }

// English: DatabaseType for local config
// 한글: 로컬 config용 DatabaseType
#include "../ServerEngine/Interfaces/DatabaseType_enum.h"
#include "../ServerEngine/Network/Core/AsyncIOProvider.h"
#include "../ServerEngine/Tests/Protocols/MessageHandler.h"
#include "../ServerEngine/Tests/Protocols/PingPong.h"
#include <mutex>
#include <string>
#include <unordered_map>
#include <atomic>
#include <memory>
#include <thread>

namespace Network::DBServer
{
// 한글: Protocols의 ConnectionId를 DBServer 범위에서 재사용한다.
using Protocols::ConnectionId;
// =============================================================================
// English: Database Server class
// ???: ?怨쀬뵠?怨뺤퓢??곷뮞 ??뺤쒔 ?????
// =============================================================================

class DBServer
{
  public:
	// English: Constructor
	// ???: ??밴쉐??
	DBServer();

	// English: Destructor
	// ???: ???늾??
	~DBServer();

	// =====================================================================
	// English: Lifecycle management
	// ???: ??몄구雅뚯눊由??온??
	// =====================================================================

	/**
	 * English: Initialize the database server
	 * ???: ?怨쀬뵠?怨뺤퓢??곷뮞 ??뺤쒔 ?λ뜃由??
	 * @param port Port number to listen on
	 * @param maxConnections Maximum allowed connections
	 * @return True if initialization succeeded
	 */
	bool Initialize(uint16_t port = 8002, size_t maxConnections = 1000);

	/**
	 * English: Start the database server
	 * ???: ?怨쀬뵠?怨뺤퓢??곷뮞 ??뺤쒔 ??뽰삂
	 * @return True if server started successfully
	 */
	bool Start();

	/**
	 * English: Stop the database server
	 * ???: ?怨쀬뵠?怨뺤퓢??곷뮞 ??뺤쒔 餓λ쵐?
	 */
	void Stop();

	/**
	 * English: Check if server is running
	 * ???: ??뺤쒔 ??쎈뻬 ?怨밴묶 ?類ㅼ뵥
	 * @return True if server is running
	 */
	bool IsRunning() const;

	// =====================================================================
	// English: Configuration
	// ???: ??쇱젟
	// =====================================================================

	/**
	 * English: Set database connection parameters
	 * ???: ?怨쀬뵠?怨뺤퓢??곷뮞 ?怨뚭퍙 ???뵬沃섎챸苑???쇱젟
	 * @param host Database host
	 * @param port Database port
	 * @param database Database name
	 * @param username Username
	 * @param password Password
	 */
	void SetDatabaseConfig(const std::string &host, uint16_t port,
							   const std::string &database,
							   const std::string &username,
							   const std::string &password);

  private:
	// =====================================================================
	// English: Network event handlers
	// ???: ??쎈뱜??곌쾿 ??源???紐껊굶??
	// =====================================================================

	/**
	 * English: Handle new connection
	 * ???: ???怨뚭퍙 筌ｌ꼶??
	 * @param connectionId Connection ID
	 */
	void OnConnectionEstablished(ConnectionId connectionId);

	/**
	 * English: Handle connection closed
	 * ???: ?怨뚭퍙 ?ル굝利?筌ｌ꼶??
	 * @param connectionId Connection ID
	 */
	void OnConnectionClosed(ConnectionId connectionId);

	/**
	 * English: Handle data received
	 * ???: ?怨쀬뵠????뤿뻿 筌ｌ꼶??
	 * @param connectionId Connection ID
	 * @param data Received data
	 * @param size Data size
	 */
	void OnDataReceived(ConnectionId connectionId, const uint8_t *data,
						size_t size);

	/**
	 * English: Handle Ping message
	 * ???: Ping 筌롫뗄?놅쭪? 筌ｌ꼶??
	 * @param message Ping message
	 */
	void OnPingMessage(const Protocols::Message &message);

	/**
	 * English: Handle Pong message
	 * 한글: Pong 메시지 처리
	 * @param message Pong message
	 */
	void OnPongMessage(const Protocols::Message &message);

	// =====================================================================
	// English: Database operations
	// ???: ?怨쀬뵠?怨뺤퓢??곷뮞 ?臾믩씜
	// =====================================================================

	/**
	 * English: Connect to database
	 * ???: ?怨쀬뵠?怨뺤퓢??곷뮞 ?怨뚭퍙
	 * @return True if connection succeeded
	 */
	bool ConnectToDatabase();

	/**
	 * English: Disconnect from database
	 * ???: ?怨쀬뵠?怨뺤퓢??곷뮞 ?怨뚭퍙 ??곸젫
	 */
	void DisconnectFromDatabase();

	/**
	 * English: Execute query
	 * ???: ?묒눖????쎈뻬
	 * @param query SQL query
	 * @return Query result
	 */
	std::string ExecuteQuery(const std::string &query);

	// =====================================================================
	// English: Private members
	// ???: ??쑨?у첎?筌롢끇苡?
	// =====================================================================

	// Network components
	std::unique_ptr<AsyncIO::AsyncIOProvider> mAsyncIOProvider;
	std::unique_ptr<Protocols::MessageHandler> mMessageHandler;
	std::unique_ptr<Protocols::PingPongHandler> mPingPongHandler;
	// 한글: Ping/Pong 레이턴시 + 시간 저장 — ServerLatencyManager로 통합 (이전: DBPingTimeManager)
	// English: Ping/Pong latency + time persistence — unified into ServerLatencyManager (was: DBPingTimeManager)
	std::unique_ptr<ServerLatencyManager> mLatencyManager;

	// Server state
	std::atomic<bool> mIsRunning;
	std::atomic<bool> mIsInitialized;
	uint16_t mPort;
	size_t mMaxConnections;

	// Database configuration
	struct DatabaseConfig
	{
		std::string host = "localhost";
		uint16_t port = 5432;
		std::string database = "networkdb";
		std::string username = "postgres";
		std::string password = "password";
		// English: Default to Mock so the server works out-of-the-box without an external DB
		// 한글: 외부 DB 없이 바로 동작하도록 기본값을 Mock으로 설정
		Network::Database::DatabaseType type = Network::Database::DatabaseType::Mock;
	} mDbConfig;

	// English: Owned database instance (created by ConnectToDatabase)
	// 한글: ConnectToDatabase가 생성하는 소유 데이터베이스 인스턴스
	std::unique_ptr<Network::Database::IDatabase> mDatabase;

	// Worker thread
	std::thread mWorkerThread;

	// Connection management
	std::unordered_map<ConnectionId, std::string> mConnections;
	std::mutex mConnectionsMutex;

	// =====================================================================
	// English: Private methods
	// ???: ??쑨?у첎?筌롫뗄???
	// =====================================================================

	/**
	 * English: Worker thread function
	 * ???: ???묽 ??살쟿????λ땾
	 */
	void WorkerThread();

	/**
	 * English: Send message to connection
	 * ???: ?怨뚭퍙嚥?筌롫뗄?놅쭪? ?袁⑸꽊
	 * @param connectionId Connection ID
	 * @param type Message type
	 * @param data Message data
	 * @param size Data size
	 */
	void SendMessage(ConnectionId connectionId, Protocols::MessageType type,
					 const void *data, size_t size);

	/**
	 * English: Get current timestamp
	 * ???: ?袁⑹삺 ???袁⑸뮞??遊?鈺곌퀬??
	 * @return Timestamp in milliseconds
	 */
	uint64_t GetCurrentTimestamp() const;
};

} // namespace Network::DBServer
