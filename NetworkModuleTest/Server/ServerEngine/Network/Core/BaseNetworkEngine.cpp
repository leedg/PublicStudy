// Base NetworkEngine implementation

#include "BaseNetworkEngine.h"
#include "NetworkEventBus.h"
#include "SessionPool.h"
#include "../../Utils/Logger.h"
#include "../../Utils/Timer.h"
#ifndef _WIN32
// ECONNRESET / EPIPE / ECONNABORTED for expected-teardown detection.
#include <cerrno>
#endif

namespace Network::Core
{

BaseNetworkEngine::BaseNetworkEngine()
	: mPort(0), mMaxConnections(0), mRunning(false), mInitialized(false)
{
	std::memset(&mStats, 0, sizeof(mStats));
}

BaseNetworkEngine::~BaseNetworkEngine()
{
	Stop();
}

// =============================================================================
// INetworkEngine interface implementation
// =============================================================================

bool BaseNetworkEngine::Initialize(size_t maxConnections, uint16_t port)
{
	if (mInitialized)
	{
		Utils::Logger::Warn("BaseNetworkEngine already initialized");
		return false;
	}

	mPort = port;
	mMaxConnections = maxConnections;
	mStats.startTime = Utils::Timer::GetCurrentTimestamp();

	// Initialize session pool (one-time, allocates all session slots).
	if (!SessionPool::Instance().Initialize(maxConnections))
	{
		Utils::Logger::Error("SessionPool initialization failed");
		return false;
	}

	// Initialize logic dispatcher (KeyedDispatcher — session-affinity worker pool).
	{
		Network::Concurrency::KeyedDispatcher::Options opts;
		opts.mWorkerCount = 4;
		opts.mQueueOptions.mBackend     = Network::Concurrency::QueueBackend::LockFree;
		opts.mQueueOptions.mCapacity    = MAX_LOGIC_QUEUE_DEPTH;
		opts.mQueueOptions.mBackpressure = Network::Concurrency::BackpressurePolicy::RejectNewest;
		opts.mName = "LogicDispatcher";
		if (!mLogicDispatcher.Initialize(opts))
		{
			Utils::Logger::Error("LogicDispatcher initialization failed");
			return false;
		}
	}

	// Initialize engine-level timer queue.
	if (!mTimerQueue.Initialize())
	{
		Utils::Logger::Error("TimerQueue initialization failed");
		return false;
	}

	// Schedule periodic session-timeout check every PING_TIMEOUT_MS/2.
	mTimerQueue.ScheduleRepeat(
		[this]() -> bool
		{
			// Check inactive sessions and close timed-out ones.
			if (!mRunning.load(std::memory_order_acquire))
			{
				return false;
			}
			const auto now = Utils::Timer::GetCurrentTimestamp();
			auto allSessions = SessionManager::Instance().GetAllSessions();
			for (auto &session : allSessions)
			{
				if (!session || !session->IsConnected())
				{
					continue;
				}
				const auto lastPing = session->GetLastPingTime();
				if (lastPing != 0 && (now - lastPing) > PING_TIMEOUT_MS)
				{
					Utils::Logger::Warn(
						"Session timeout - ID: " + std::to_string(session->GetId()));
					CloseConnection(session->GetId());
				}
			}
			return mRunning.load(std::memory_order_acquire);
		},
		PING_TIMEOUT_MS / 2);

	// Call platform-specific initialization
	if (!InitializePlatform())
	{
		Utils::Logger::Error("Platform initialization failed");
		return false;
	}

	mInitialized.store(true, std::memory_order_release);
	Utils::Logger::Info("BaseNetworkEngine initialized on port " +
						std::to_string(mPort));
	return true;
}

bool BaseNetworkEngine::Start()
{
	if (!mInitialized)
	{
		Utils::Logger::Error("BaseNetworkEngine not initialized");
		return false;
	}

	if (mRunning)
	{
		Utils::Logger::Warn("BaseNetworkEngine already running");
		return false;
	}

	mRunning.store(true, std::memory_order_release);

	// Start platform-specific I/O
	if (!StartPlatformIO())
	{
		Utils::Logger::Error("Failed to start platform I/O");
		mRunning.store(false, std::memory_order_release);
		return false;
	}

	Utils::Logger::Info("BaseNetworkEngine started");
	return true;
}

void BaseNetworkEngine::Stop()
{
	if (!mRunning)
	{
		return;
	}

	mRunning.store(false, std::memory_order_release);

	// Stop timer queue first (cancels periodic session-timeout checks).
	mTimerQueue.Shutdown();

	// Stop platform-specific I/O
	StopPlatformIO();

	// Close all sessions
	SessionManager::Instance().CloseAllSessions();

	// Shutdown logic dispatcher after all sessions are closed.
	//          CloseAllSessions() calls session->Close() which cancels AsyncScope,
	//          causing pending-but-not-yet-running disconnect tasks to be silently
	//          skipped inside WorkerThreadFunc. Applications may not receive
	//          OnDisconnected callbacks for sessions that had pending tasks at
	//          shutdown time — this is intentional (engine is stopping).
	mLogicDispatcher.Shutdown();

	// Shutdown platform resources
	ShutdownPlatform();

	mInitialized.store(false, std::memory_order_release);
	Utils::Logger::Info("BaseNetworkEngine stopped");
}

bool BaseNetworkEngine::IsRunning() const
{
	return mRunning;
}

bool BaseNetworkEngine::RegisterEventCallback(NetworkEvent eventType,
												  NetworkEventCallback callback)
{
	std::lock_guard<std::mutex> lock(mCallbackMutex);
	mCallbacks[eventType] = std::move(callback);
	return true;
}

void BaseNetworkEngine::UnregisterEventCallback(NetworkEvent eventType)
{
	std::lock_guard<std::mutex> lock(mCallbackMutex);
	mCallbacks.erase(eventType);
}

bool BaseNetworkEngine::SendData(Utils::ConnectionId connectionId,
								 const void *data, size_t size)
{
	if (!data || size == 0)
	{
		return false;
	}

	auto session = SessionManager::Instance().GetSession(connectionId);
	if (!session || !session->IsConnected())
	{
		return false;
	}

	// Session::Send now returns SendResult — check result directly.
	const auto sendResult = session->Send(data, static_cast<uint32_t>(size));
	if (sendResult != Session::SendResult::Ok)
	{
		return false;
	}

	mTotalBytesSent.fetch_add(size, std::memory_order_relaxed);

	return true;
}

void BaseNetworkEngine::CloseConnection(Utils::ConnectionId connectionId)
{
	auto session = SessionManager::Instance().GetSession(connectionId);
	if (!session)
	{
		return;
	}

	// Dispatch OnDisconnected + FireEvent to the same worker as this session's
	//          ProcessRawRecv tasks (KeyedDispatcher key = sessionId).
	//          This ensures Close() and ProcessRawRecv are always serialized for the session.
	//          The session shared_ptr keeps the object alive until the task runs.
	auto sessionCopy = session;
	// Route through AsyncScope — consistent with ProcessRecvCompletion disconnect path.
	//          If Close() was already called (Cancel() set), the event is silently dropped.
	if (!session->mAsyncScope.Submit(
			mLogicDispatcher,
			connectionId,
			[this, sessionCopy, connectionId]()
			{
				sessionCopy->OnDisconnected();
				FireEvent(NetworkEvent::Disconnected, connectionId);
			}))
	{
		Utils::Logger::Warn("LogicDispatcher full or scope cancelled - disconnect event dropped, Session: " +
		                    std::to_string(connectionId));
	}

	// Remove from manager immediately (caller's thread) — same as ProcessRecvCompletion
	SessionManager::Instance().RemoveSession(session);
}

std::string
BaseNetworkEngine::GetConnectionInfo(Utils::ConnectionId connectionId) const
{
	auto session = SessionManager::Instance().GetSession(connectionId);
	if (!session)
	{
		return "";
	}

	return "Session[" + std::to_string(connectionId) +
			   "] State=" + std::to_string(static_cast<int>(session->GetState()));
}

INetworkEngine::Statistics BaseNetworkEngine::GetStatistics() const
{
	std::lock_guard<std::mutex> lock(mStatsMutex);
	Statistics stats = mStats;
	stats.totalBytesSent     = mTotalBytesSent.load(std::memory_order_relaxed);
	stats.totalBytesReceived = mTotalBytesReceived.load(std::memory_order_relaxed);
	stats.totalConnections   = mTotalConnections.load(std::memory_order_relaxed);
	stats.activeConnections  = SessionManager::Instance().GetSessionCount();
	stats.totalSendErrors    = mTotalSendErrors.load(std::memory_order_relaxed);
	stats.totalRecvErrors    = mTotalRecvErrors.load(std::memory_order_relaxed);
	stats.totalErrors        = stats.totalSendErrors + stats.totalRecvErrors;
	return stats;
}

// =============================================================================
// Helper methods for derived classes
// =============================================================================

void BaseNetworkEngine::FireEvent(NetworkEvent eventType,
									  Utils::ConnectionId connId,
									  const uint8_t *data, size_t dataSize,
									  OSError errorCode)
{
	NetworkEventCallback callback;
	{
		std::lock_guard<std::mutex> lock(mCallbackMutex);
		auto it = mCallbacks.find(eventType);
		if (it != mCallbacks.end())
		{
			callback = it->second;
		}
	}

	// Create event data
	NetworkEventData eventData;
	eventData.eventType = eventType;
	eventData.connectionId = connId;
	eventData.dataSize = dataSize;
	eventData.errorCode = errorCode;
	eventData.timestamp = Utils::Timer::GetCurrentTimestamp();

	if (data && dataSize > 0)
	{
		eventData.data = std::make_unique<uint8_t[]>(dataSize);
		std::memcpy(eventData.data.get(), data, dataSize);
	}

	// Call callback
	if (callback)
	{
		callback(eventData);
	}

	// Publish to multi-subscriber event bus (NetworkEventBus).
	//          NetworkBusEventData is copyable so it can be sent to multiple channels.
	{
		NetworkBusEventData busData;
		busData.eventType    = eventData.eventType;
		busData.connectionId = eventData.connectionId;
		busData.dataSize     = eventData.dataSize;
		busData.errorCode    = eventData.errorCode;
		busData.timestamp    = eventData.timestamp;
		if (eventData.data && eventData.dataSize > 0)
		{
			busData.data.assign(eventData.data.get(),
			                    eventData.data.get() + eventData.dataSize);
		}
		NetworkEventBus::Instance().Publish(eventData.eventType, busData);
	}
}

void BaseNetworkEngine::ProcessRecvCompletion(SessionRef session,
												  int32_t bytesReceived,
												  const char *data)
{
	if (!session || !session->IsConnected())
	{
		return;
	}

	if (bytesReceived <= 0)
	{
		// Connection closed or error — route through AsyncScope so that if
		//          Close() was called first (Cancel() already set), the event is silently
		//          dropped instead of firing OnDisconnected() on an already-closed session.
		const auto connId = session->GetId();
		auto sessionCopy  = session;
		if (!session->mAsyncScope.Submit(
				mLogicDispatcher,
				connId,
				[this, sessionCopy, connId]()
				{
					sessionCopy->OnDisconnected();
					FireEvent(NetworkEvent::Disconnected, connId);
				}))
		{
			Utils::Logger::Warn(
				"LogicDispatcher full or scope cancelled - disconnect event dropped, Session: " +
				std::to_string(connId));
		}

		SessionManager::Instance().RemoveSession(session);
		return;
	}

	// Update stats (atomic, no lock needed)
	mTotalBytesReceived.fetch_add(bytesReceived, std::memory_order_relaxed);

	// Dispatch via AsyncScope so that pending tasks are skipped after session Close().
	//          KeyedDispatcher key = sessionId guarantees FIFO order per session.
	const auto connId = session->GetId();
	auto sessionCopy  = session;
	auto dataCopy     = std::make_shared<std::vector<char>>(
        static_cast<size_t>(bytesReceived));
	std::memcpy(dataCopy->data(), data, static_cast<size_t>(bytesReceived));

	if (!session->mAsyncScope.Submit(
			mLogicDispatcher,
			connId,
			[this, sessionCopy, dataCopy, bytesReceived]()
			{
				const char *recvData = dataCopy->data();
				sessionCopy->ProcessRawRecv(recvData,
				                            static_cast<uint32_t>(bytesReceived));
				// DataReceived carries raw TCP segment bytes, NOT necessarily
				//          complete application packets. ProcessRawRecv handles packet
				//          assembly internally and invokes OnRecv per complete packet.
				//          Use OnRecv / SetOnRecv for application packet processing;
				//          DataReceived is intended for raw byte monitoring/tracing only.
				FireEvent(NetworkEvent::DataReceived, sessionCopy->GetId(),
				          reinterpret_cast<const uint8_t *>(recvData), bytesReceived);
			}))
	{
		Utils::Logger::Warn("Logic queue full - recv dropped, disconnecting Session: " +
		                    std::to_string(connId));
		SessionManager::Instance().RemoveSession(session);
	}
}

void BaseNetworkEngine::ProcessSendCompletion(SessionRef session,
											  int32_t bytesSent)
{
	if (!session)
	{
		return;
	}

	// A zero or negative completion on the send path means the remote
	//          closed the connection or an OS-level error occurred. Route through
	//          ProcessErrorCompletion so the session is cleanly disconnected and
	//          send error stats are updated. Firing DataSent or calling PostSend
	//          on such a completion would be incorrect.
	if (bytesSent <= 0)
	{
		ProcessErrorCompletion(session, AsyncIO::AsyncIOType::Send, 0);
		return;
	}

	// Fire DataSent event
	FireEvent(NetworkEvent::DataSent, session->GetId());

	// Continue sending if queue has more data
	if (!session->PostSend())
	{
		Utils::Logger::Debug("Send queue empty for session " +
							 std::to_string(session->GetId()));
	}
}

void BaseNetworkEngine::ProcessErrorCompletion(SessionRef session,
                                               AsyncIO::AsyncIOType ioType,
                                               OSError osError)
{
	if (!session)
	{
		return;
	}

	const auto connId = session->GetId();

	// Increment the per-direction counter before routing disconnect.
	//          Send and Recv errors are tracked separately so callers can diagnose
	//          whether failures are on the inbound or outbound path.
	if (ioType == AsyncIO::AsyncIOType::Send)
	{
		mTotalSendErrors.fetch_add(1, std::memory_order_relaxed);
	}
	else
	{
		mTotalRecvErrors.fetch_add(1, std::memory_order_relaxed);
	}

	// Log at Warn for expected connection-teardown error codes — these occur
	//          on every normal remote disconnect and should not alarm on-call operators.
	//          Log at Error for unexpected codes that may indicate a real problem.
	//          osError == 0 means "failed to queue next I/O operation" — also Warn,
	//          since the underlying error was already logged by the caller (QueueRecv).
#ifdef _WIN32
	const bool isExpectedClose = (osError == WSAECONNRESET   ||
	                              osError == WSAECONNABORTED  ||
	                              osError == WSAESHUTDOWN     ||
	                              osError == 0);
#else
	const bool isExpectedClose = (osError == ECONNRESET  ||
	                              osError == EPIPE        ||
	                              osError == ECONNABORTED ||
	                              osError == 0);
#endif

	const std::string direction = (ioType == AsyncIO::AsyncIOType::Send) ? "Send" : "Recv";
	const std::string msg = direction + " error on Session " + std::to_string(connId) +
	                        " - OS error: " + std::to_string(static_cast<uint32_t>(osError));
	if (isExpectedClose)
	{
		Utils::Logger::Warn(msg);
	}
	else
	{
		Utils::Logger::Error(msg);
	}

	// Route through ProcessRecvCompletion(bytesReceived=0) so that the
	//          disconnect event is always submitted via session->mAsyncScope.
	//          This prevents OnDisconnected() from firing on an already-closed session
	//          (double-event) when Close() was called concurrently from another path.
	ProcessRecvCompletion(session, 0, nullptr);
}

} // namespace Network::Core
