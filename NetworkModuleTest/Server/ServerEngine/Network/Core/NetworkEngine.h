#pragma once

// Core network abstraction layer for NetworkModule

#include "../../Utils/NetworkUtils.h"
#include "AsyncIOProvider.h"
#include <functional>
#include <memory>

namespace Network::Core
{
// Import utility types into Core namespace
using Utils::ConnectionId;
using Utils::Timestamp;
// =============================================================================
// Network event types
// =============================================================================

enum class NetworkEvent : uint8_t
{
	// New connection established
	Connected,

	// Connection closed
	Disconnected,

	// Data received
	DataReceived,

	// Data sent successfully
	DataSent,

	// Error occurred
	Error
};

// =============================================================================
// Network event data
// =============================================================================

struct NetworkEventData
{
	NetworkEvent eventType;
	ConnectionId connectionId;
	size_t dataSize;
	OSError errorCode;
	Timestamp timestamp;
	std::unique_ptr<uint8_t[]> data;
};

// =============================================================================
// Event callback type
// =============================================================================

using NetworkEventCallback = std::function<void(const NetworkEventData &)>;

// =============================================================================
// Core network interface
// =============================================================================

class INetworkEngine
{
  public:
	// Virtual destructor
	virtual ~INetworkEngine() = default;

	// =====================================================================
	// Lifecycle management
	// =====================================================================

	/**
	 * Initialize the network engine
	 * @param maxConnections Maximum allowed connections
	 * @param port Port number to listen on
	 * @return True if initialization succeeded
	 */
	virtual bool Initialize(size_t maxConnections, uint16_t port) = 0;

	/**
	 * Start the network engine
	 * @return True if started successfully
	 */
	virtual bool Start() = 0;

	/**
	 * Stop the network engine
	 */
	virtual void Stop() = 0;

	/**
	 * Check if engine is running
	 * @return True if running
	 */
	virtual bool IsRunning() const = 0;

	// =====================================================================
	// Event handling
	// =====================================================================

	/**
	 * Register event callback
	 * @param eventType Event type to register for
	 * @param callback Callback function
	 * @return True if registration succeeded
	 */
	virtual bool RegisterEventCallback(NetworkEvent eventType,
										   NetworkEventCallback callback) = 0;

	/**
	 * Unregister event callback
	 * @param eventType Event type
	 */
	virtual void UnregisterEventCallback(NetworkEvent eventType) = 0;

	// =====================================================================
	// Connection management
	// =====================================================================

	/**
	 * Send data to specific connection
	 * @param connectionId Connection ID
	 * @param data Data to send
	 * @param size Data size
	 * @return True if send initiated successfully
	 */
	virtual bool SendData(ConnectionId connectionId, const void *data,
						  size_t size) = 0;

	/**
	 * Close specific connection
	 * @param connectionId Connection ID
	 */
	virtual void CloseConnection(ConnectionId connectionId) = 0;

	/**
	 * Get connection information
	 * @param connectionId Connection ID
	 * @return Connection info or empty if not found
	 */
	virtual std::string GetConnectionInfo(ConnectionId connectionId) const = 0;

	// =====================================================================
	// Statistics
	// =====================================================================

	struct Statistics
	{
		uint64_t totalConnections;
		uint64_t activeConnections;
		uint64_t totalBytesSent;
		uint64_t totalBytesReceived;
		// Per-direction error counters. totalErrors == sendErrors + recvErrors.
		uint64_t totalSendErrors;
		uint64_t totalRecvErrors;
		uint64_t totalErrors;
		Timestamp startTime;
	};

	/**
	 * Get engine statistics
	 * @return Statistics object
	 */
	virtual Statistics GetStatistics() const = 0;
};

// =============================================================================
// Factory function
// =============================================================================

/**
 * Create network engine instance
 * @param engineType Engine type (e.g., "auto", "epoll", "iocp")
 * @return Network engine instance or nullptr
 */
std::unique_ptr<INetworkEngine>
CreateNetworkEngine(const std::string &engineType = "auto");

/**
 * Get list of available engine types
 * @return Vector of engine type names
 */
std::vector<std::string> GetAvailableEngineTypes();

} // namespace Network::Core
