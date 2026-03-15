#pragma once

// Base implementation of INetworkEngine with common logic
//
// Design: Template Method Pattern
// - Common logic in base class (Session management, events, stats)
// - Platform-specific logic in derived classes (socket, I/O)
//

#include "../../Concurrency/KeyedDispatcher.h"
#include "../../Concurrency/TimerQueue.h"
#include "../../Utils/NetworkUtils.h"
#include "AsyncIOProvider.h"
#include "NetworkEngine.h"
#include "Session.h"
#include "SessionManager.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Network::Core
{

// =============================================================================
// Base NetworkEngine - implements common functionality
// =============================================================================

class BaseNetworkEngine : public INetworkEngine
{
  public:
	BaseNetworkEngine();
	virtual ~BaseNetworkEngine();

	// =====================================================================
	// INetworkEngine interface (final implementation)
	// =====================================================================

	bool Initialize(size_t maxConnections, uint16_t port) override final;
	bool Start() override final;
	void Stop() override final;
	bool IsRunning() const override final;

	bool RegisterEventCallback(NetworkEvent eventType,
								   NetworkEventCallback callback) override final;
	void UnregisterEventCallback(NetworkEvent eventType) override final;

	bool SendData(Utils::ConnectionId connectionId, const void *data,
				  size_t size) override final;
	void CloseConnection(Utils::ConnectionId connectionId) override final;
	std::string
	GetConnectionInfo(Utils::ConnectionId connectionId) const override final;

	Statistics GetStatistics() const override final;

  protected:
	// =====================================================================
	// Platform-specific hooks (must implement in derived classes)
	// =====================================================================

	/**
	 * Initialize platform-specific resources
	 * @return True if initialization succeeded
	 */
	virtual bool InitializePlatform() = 0;

	/**
	 * Shutdown platform-specific resources
	 */
	virtual void ShutdownPlatform() = 0;

	/**
	 * Start platform-specific I/O threads
	 * @return True if start succeeded
	 */
	virtual bool StartPlatformIO() = 0;

	/**
	 * Stop platform-specific I/O threads
	 */
	virtual void StopPlatformIO() = 0;

	/**
	 * Accept loop (platform-specific)
	 */
	virtual void AcceptLoop() = 0;

	/**
	 * Process I/O completions (platform-specific)
	 */
	virtual void ProcessCompletions() = 0;

	// =====================================================================
	// Helper methods for derived classes
	// =====================================================================

	/**
	 * Fire network event to registered callbacks
	 */
	void FireEvent(NetworkEvent eventType, Utils::ConnectionId connId,
				   const uint8_t *data = nullptr, size_t dataSize = 0,
				   OSError errorCode = 0);

	/**
	 * Process received data from completion
	 */
	void ProcessRecvCompletion(SessionRef session, int32_t bytesReceived,
								   const char *data);

	/**
	 * Process send completion
	 */
	void ProcessSendCompletion(SessionRef session, int32_t bytesSent);

	/**
	 * Handle an I/O error completion — increments the per-direction error counter
	 *          (mTotalSendErrors or mTotalRecvErrors) and routes the disconnect through
	 *          ProcessRecvCompletion(0) so that session->mAsyncScope is always respected.
	 *          Call this instead of ProcessRecvCompletion(session, 0, nullptr) directly
	 *          whenever a completion entry carries an OS error or a non-positive result,
	 *          so that Send and Recv errors are tracked separately.
	 * @param session  The session the error occurred on
	 * @param ioType   AsyncIOType::Send or AsyncIOType::Recv
	 * @param osError  OS-level error code (0 if not available)
	 */
	void ProcessErrorCompletion(SessionRef session,
	                            AsyncIO::AsyncIOType ioType,
	                            OSError osError);

  protected:
	// =====================================================================
	// Common member variables
	// =====================================================================

	// Async I/O provider (platform-specific backend).
	//          shared_ptr so that Session objects can hold a copy (via SetAsyncProvider)
	//          and the provider stays alive until all sessions release their reference.
	std::shared_ptr<AsyncIO::AsyncIOProvider> mProvider;

	// Configuration
	uint16_t mPort;
	size_t mMaxConnections;

	// State
	std::atomic<bool> mRunning;
	std::atomic<bool> mInitialized;

	// Event callbacks
	std::unordered_map<NetworkEvent, NetworkEventCallback> mCallbacks;
	mutable std::mutex mCallbackMutex;

	// Key-affinity dispatcher for ordered async logic execution.
	//          Same sessionId always routes to the same worker → per-session FIFO order.
	Network::Concurrency::KeyedDispatcher mLogicDispatcher;

	// Engine-level timer queue for session-timeout checks and other periodic tasks.
	Network::Concurrency::TimerQueue mTimerQueue;

	// Statistics - hot-path counters are atomic; cold-path data uses mutex.
	//          Send/Recv error counters are tracked separately so GetStatistics() can
	//          report per-direction breakdown. totalErrors = sendErrors + recvErrors.
	//       totalErrors = sendErrors + recvErrors.
	mutable std::mutex mStatsMutex;
	Statistics mStats;
	std::atomic<uint64_t> mTotalBytesSent{0};
	std::atomic<uint64_t> mTotalBytesReceived{0};
	std::atomic<uint64_t> mTotalConnections{0};
	std::atomic<uint64_t> mTotalSendErrors{0};
	std::atomic<uint64_t> mTotalRecvErrors{0};
};

} // namespace Network::Core
