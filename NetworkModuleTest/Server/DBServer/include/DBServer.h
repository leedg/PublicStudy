#pragma once

// Database Server main header

// DBPingTimeManager replaced by ServerLatencyManager (merged) — see DBPingTimeManager.h for migration notes
#include "ServerLatencyManager.h"

// Forward-declare IDatabase; full definition included in DBServer.cpp
namespace Network { namespace Database { class IDatabase; } }

// DatabaseType for local config
#include "../ServerEngine/Interfaces/DatabaseType_enum.h"
#include "../ServerEngine/Utils/NetworkTypes.h"
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
using Protocols::ConnectionId;
// =============================================================================
// Database Server class
// =============================================================================

class DBServer
{
  public:
	// Constructor
	DBServer();

	// Destructor
	~DBServer();

	// =====================================================================
	// Lifecycle management
	// =====================================================================

	/**
	 * Initialize the database server
	 * @param port Port number to listen on
	 * @param maxConnections Maximum allowed connections
	 * @return True if initialization succeeded
	 */
	bool Initialize(uint16_t port = Network::Utils::DEFAULT_TEST_DB_PORT, size_t maxConnections = 1000);

	/**
	 * Start the database server
	 * @return True if server started successfully
	 */
	bool Start();

	/**
	 * Stop the database server
	 */
	void Stop();

	/**
	 * Check if server is running
	 * @return True if server is running
	 */
	bool IsRunning() const;

	// =====================================================================
	// Configuration
	// =====================================================================

	/**
	 * Set database connection parameters
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
	// Network event handlers
	// =====================================================================

	/**
	 * Handle new connection
	 * @param connectionId Connection ID
	 */
	void OnConnectionEstablished(ConnectionId connectionId);

	/**
	 * Handle connection closed
	 * @param connectionId Connection ID
	 */
	void OnConnectionClosed(ConnectionId connectionId);

	/**
	 * Handle data received
	 * @param connectionId Connection ID
	 * @param data Received data
	 * @param size Data size
	 */
	void OnDataReceived(ConnectionId connectionId, const uint8_t *data,
						size_t size);

	/**
	 * Handle Ping message
	 * @param message Ping message
	 */
	void OnPingMessage(const Protocols::Message &message);

	/**
	 * Handle Pong message
	 * @param message Pong message
	 */
	void OnPongMessage(const Protocols::Message &message);

	// =====================================================================
	// Database operations
	// =====================================================================

	/**
	 * Connect to database
	 * @return True if connection succeeded
	 */
	bool ConnectToDatabase();

	/**
	 * Disconnect from database
	 */
	void DisconnectFromDatabase();

	/**
	 * Execute query
	 * @param query SQL query
	 * @return Query result
	 */
	std::string ExecuteQuery(const std::string &query);

	// =====================================================================
	// Private members
	// =====================================================================

	// Network components
	std::unique_ptr<AsyncIO::AsyncIOProvider> mAsyncIOProvider;
	std::unique_ptr<Protocols::MessageHandler> mMessageHandler;
	std::unique_ptr<Protocols::PingPongHandler> mPingPongHandler;
	// Ping/Pong latency + time persistence — unified into ServerLatencyManager (was: DBPingTimeManager)
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
		// Default to Mock so the server works out-of-the-box without an external DB
		Network::Database::DatabaseType type = Network::Database::DatabaseType::Mock;
	} mDbConfig;

	// Owned database instance (created by ConnectToDatabase)
	std::unique_ptr<Network::Database::IDatabase> mDatabase;

	// Worker thread
	std::thread mWorkerThread;

	// Connection management
	std::unordered_map<ConnectionId, std::string> mConnections;
	std::mutex mConnectionsMutex;

	// =====================================================================
	// Private methods
	// =====================================================================

	/**
	 * Worker thread function
	 */
	void WorkerThread();

	/**
	 * Send message to connection
	 * @param connectionId Connection ID
	 * @param type Message type
	 * @param data Message data
	 * @param size Data size
	 */
	void SendMessage(ConnectionId connectionId, Protocols::MessageType type,
					 const void *data, size_t size);

	/**
	 * Get current timestamp
	 * @return Timestamp in milliseconds
	 */
	uint64_t GetCurrentTimestamp() const;
};

} // namespace Network::DBServer
