// English: Base NetworkEngine implementation
// 한글: 기본 NetworkEngine 구현

#include "BaseNetworkEngine.h"
#include "NetworkEventBus.h"
#include "SessionPool.h"
#include "../../Utils/Logger.h"
#include "../../Utils/Timer.h"

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
// English: INetworkEngine interface implementation
// 한글: INetworkEngine 인터페이스 구현
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

	// English: Initialize session pool (one-time, allocates all session slots).
	// 한글: 세션 풀 초기화 (1회, 모든 세션 슬롯 사전 할당).
	if (!SessionPool::Instance().Initialize(maxConnections))
	{
		Utils::Logger::Error("SessionPool initialization failed");
		return false;
	}

	// English: Initialize logic dispatcher (KeyedDispatcher — session-affinity worker pool).
	// 한글: 로직 디스패처 초기화 (KeyedDispatcher — 세션 친화도 워커 풀).
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

	// English: Initialize engine-level timer queue.
	// 한글: 엔진 수준 타이머 큐 초기화.
	if (!mTimerQueue.Initialize())
	{
		Utils::Logger::Error("TimerQueue initialization failed");
		return false;
	}

	// English: Schedule periodic session-timeout check every PING_TIMEOUT_MS/2.
	// 한글: PING_TIMEOUT_MS/2 주기로 세션 타임아웃 점검 등록.
	mTimerQueue.ScheduleRepeat(
		[this]() -> bool
		{
			// English: Check inactive sessions and close timed-out ones.
			// 한글: 비활성 세션 점검 및 타임아웃 세션 종료.
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

	// English: Call platform-specific initialization
	// 한글: 플랫폼별 초기화 호출
	if (!InitializePlatform())
	{
		Utils::Logger::Error("Platform initialization failed");
		return false;
	}

	mInitialized.store(true, std::memory_order_relaxed);
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

	mRunning.store(true, std::memory_order_relaxed);

	// English: Start platform-specific I/O
	// 한글: 플랫폼별 I/O 시작
	if (!StartPlatformIO())
	{
		Utils::Logger::Error("Failed to start platform I/O");
		mRunning.store(false, std::memory_order_relaxed);
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

	mRunning.store(false, std::memory_order_relaxed);

	// English: Stop timer queue first (cancels periodic session-timeout checks).
	// 한글: 타이머 큐 먼저 종료 (주기 세션 타임아웃 점검 취소).
	mTimerQueue.Shutdown();

	// English: Stop platform-specific I/O
	// 한글: 플랫폼별 I/O 중지
	StopPlatformIO();

	// English: Close all sessions
	// 한글: 모든 세션 종료
	SessionManager::Instance().CloseAllSessions();

	// English: Shutdown logic dispatcher after all sessions are closed.
	// 한글: 모든 세션 종료 후 로직 디스패처 종료.
	mLogicDispatcher.Shutdown();

	// English: Shutdown platform resources
	// 한글: 플랫폼 리소스 종료
	ShutdownPlatform();

	mInitialized.store(false, std::memory_order_relaxed);
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

	// English: Session::Send now returns SendResult — check result directly.
	// 한글: Session::Send가 SendResult를 반환하므로 결과를 직접 확인.
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

	// English: Dispatch OnDisconnected + FireEvent to the same worker as this session's
	//          ProcessRawRecv tasks (KeyedDispatcher key = sessionId).
	//          This ensures Close() and ProcessRawRecv are always serialized for the session.
	//          The session shared_ptr keeps the object alive until the task runs.
	// 한글: OnDisconnected + FireEvent를 이 세션의 ProcessRawRecv 작업과 동일한
	//       워커로 디스패치 (KeyedDispatcher key = sessionId).
	//       Close()와 ProcessRawRecv가 항상 세션 단위로 직렬화됨.
	//       세션 shared_ptr이 작업 실행 전까지 객체를 살아있게 유지.
	auto sessionCopy = session;
	if (!mLogicDispatcher.Dispatch(
			connectionId,
			[this, sessionCopy, connectionId]()
			{
				sessionCopy->OnDisconnected();
				FireEvent(NetworkEvent::Disconnected, connectionId);
			}))
	{
		Utils::Logger::Warn("LogicDispatcher full - disconnect event dropped, Session: " +
		                    std::to_string(connectionId));
	}

	// English: Remove from manager immediately (caller's thread) — same as ProcessRecvCompletion
	// 한글: 호출 스레드에서 즉시 매니저에서 제거 — ProcessRecvCompletion과 동일한 패턴
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
	stats.totalBytesSent = mTotalBytesSent.load(std::memory_order_relaxed);
	stats.totalBytesReceived = mTotalBytesReceived.load(std::memory_order_relaxed);
	stats.totalConnections = mTotalConnections.load(std::memory_order_relaxed);
	stats.activeConnections = SessionManager::Instance().GetSessionCount();
	return stats;
}

// =============================================================================
// English: Helper methods for derived classes
// 한글: 파생 클래스용 헬퍼 메서드
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
		if (it == mCallbacks.end())
		{
			return;
		}
		callback = it->second;
	}

	if (!callback)
	{
		return;
	}

	// English: Create event data
	// 한글: 이벤트 데이터 생성
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

	// English: Call callback
	// 한글: 콜백 호출
	callback(eventData);

	// English: Publish to multi-subscriber event bus (NetworkEventBus).
	//          NetworkBusEventData is copyable so it can be sent to multiple channels.
	// 한글: 다중 구독자 이벤트 버스에 발행 (NetworkEventBus).
	//       NetworkBusEventData는 복사 가능하여 다수의 채널에 전달 가능.
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
		// English: Connection closed or error — dispatch to session's dedicated worker.
		// 한글: 연결 종료 또는 에러 — 세션 전용 워커로 디스패치.
		const auto connId = session->GetId();
		auto sessionCopy  = session;
		if (!mLogicDispatcher.Dispatch(
				connId,
				[this, sessionCopy, connId]()
				{
					sessionCopy->OnDisconnected();
					FireEvent(NetworkEvent::Disconnected, connId);
				}))
		{
			Utils::Logger::Warn(
				"LogicDispatcher full - disconnect event dropped, Session: " +
				std::to_string(connId));
		}

		SessionManager::Instance().RemoveSession(session);
		return;
	}

	// English: Update stats (atomic, no lock needed)
	// 한글: 통계 업데이트 (atomic, 락 불필요)
	mTotalBytesReceived.fetch_add(bytesReceived, std::memory_order_relaxed);

	// English: Dispatch via AsyncScope so that pending tasks are skipped after session Close().
	//          KeyedDispatcher key = sessionId guarantees FIFO order per session.
	// 한글: AsyncScope를 통해 디스패치하여 세션 Close() 이후 대기 작업 건너뜀.
	//       KeyedDispatcher key = sessionId로 세션 단위 FIFO 순서 보장.
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

	// English: Fire DataSent event
	// 한글: DataSent 이벤트 발생
	FireEvent(NetworkEvent::DataSent, session->GetId());

	// English: Continue sending if queue has more data
	// 한글: 큐에 더 많은 데이터가 있으면 계속 전송
	if (!session->PostSend())
	{
		Utils::Logger::Debug("Send queue empty for session " +
							 std::to_string(session->GetId()));
	}
}

} // namespace Network::Core
