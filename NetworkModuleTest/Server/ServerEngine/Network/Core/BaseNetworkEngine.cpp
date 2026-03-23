// 기본 NetworkEngine 구현

#include "BaseNetworkEngine.h"
#include "NetworkEventBus.h"
#include "SessionPool.h"
#include "../../Utils/Logger.h"
#include "../../Utils/Timer.h"
#ifndef _WIN32
// ECONNRESET / EPIPE / ECONNABORTED for expected-teardown detection.
// 한글: 정상 teardown 판별용 ECONNRESET / EPIPE / ECONNABORTED.
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
// INetworkEngine 인터페이스 구현
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

	// 세션 풀 초기화 (1회, 모든 세션 슬롯 사전 할당).
	if (!SessionPool::Instance().Initialize(maxConnections))
	{
		Utils::Logger::Error("SessionPool initialization failed");
		return false;
	}

	// 로직 디스패처 초기화 (KeyedDispatcher — 세션 친화도 워커 풀).
	{
		Network::Concurrency::KeyedDispatcher::Options opts;
		// 워커 4개 — CPU 바운드 패킷 파싱 부하를 여러 스레드로 분산하면서도
		// 세션 친화도로 동일 세션 처리를 단일 워커에 직렬화한다.
		opts.mWorkerCount = 4;
		opts.mQueueOptions.mCapacity    = MAX_LOGIC_QUEUE_DEPTH;
		opts.mQueueOptions.mBackpressure = Network::Concurrency::BackpressurePolicy::RejectNewest;
		opts.mName = "LogicDispatcher";
		if (!mLogicDispatcher.Initialize(opts))
		{
			Utils::Logger::Error("LogicDispatcher initialization failed");
			return false;
		}
	}

	// 엔진 수준 타이머 큐 초기화.
	if (!mTimerQueue.Initialize())
	{
		Utils::Logger::Error("TimerQueue initialization failed");
		return false;
	}

	// PING_TIMEOUT_MS/2 주기로 세션 타임아웃 점검 등록.
	// PING_TIMEOUT_MS의 절반 주기로 검사하면 최악의 경우에도 PING_TIMEOUT_MS 이내에
	// 비활성 세션을 감지할 수 있다.
	mTimerQueue.ScheduleRepeat(
		[this]() -> bool
		{
			// 비활성 세션 점검 및 타임아웃 세션 종료.
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

	// 플랫폼별 초기화 호출
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

	// 플랫폼별 I/O 시작
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

	// 타이머 큐 먼저 종료 (주기 세션 타임아웃 점검 취소).
	// 타이머를 먼저 내리지 않으면 CloseAllSessions() 이후에도 타임아웃 콜백이
	// 이미 닫힌 세션에 접근하려 할 수 있다.
	mTimerQueue.Shutdown();

	// 플랫폼별 I/O 중지
	StopPlatformIO();

	// 모든 세션 종료
	SessionManager::Instance().CloseAllSessions();

	// Shutdown logic dispatcher after all sessions are closed.
	//          CloseAllSessions() calls session->Close() which cancels AsyncScope,
	//          causing pending-but-not-yet-running disconnect tasks to be silently
	//          skipped inside WorkerThreadFunc. Applications may not receive
	//          OnDisconnected callbacks for sessions that had pending tasks at
	//          shutdown time — this is intentional (engine is stopping).
	// 한글: 모든 세션 종료 후 로직 디스패처 종료.
	//       CloseAllSessions()가 session->Close()를 호출하여 AsyncScope를 취소하므로
	//       아직 실행되지 않은 disconnect 태스크는 WorkerThreadFunc 내에서 조용히 건너뜀.
	//       종료 시 대기 중인 태스크가 있던 세션에 대해 OnDisconnected 콜백이
	//       전달되지 않을 수 있음 — 엔진이 종료 중이므로 의도된 동작임.
	mLogicDispatcher.Shutdown();

	// 플랫폼 리소스 종료
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

	// Session::Send가 SendResult를 반환하므로 결과를 직접 확인.
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
	// 한글: OnDisconnected + FireEvent를 이 세션의 ProcessRawRecv 작업과 동일한
	//       워커로 디스패치 (KeyedDispatcher key = sessionId).
	//       Close()와 ProcessRawRecv가 항상 세션 단위로 직렬화됨.
	//       세션 shared_ptr이 작업 실행 전까지 객체를 살아있게 유지.
	auto sessionCopy = session;
	// Route through AsyncScope — consistent with ProcessRecvCompletion disconnect path.
	//          If Close() was already called (Cancel() set), the event is silently dropped.
	// 한글: AsyncScope 경유 — ProcessRecvCompletion disconnect path와 일관성 유지.
	//       Close()가 이미 호출된 경우 (Cancel() 완료) 이벤트를 조용히 건너뜀.
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

	// 호출 스레드에서 즉시 매니저에서 제거 — ProcessRecvCompletion과 동일한 패턴.
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
// 파생 클래스용 헬퍼 메서드
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

	// 이벤트 데이터 생성
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

	if (callback)
	{
		callback(eventData);
	}

	// Publish to multi-subscriber event bus (NetworkEventBus).
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
		// Connection closed or error — route through AsyncScope so that if
		//          Close() was called first (Cancel() already set), the event is silently
		//          dropped instead of firing OnDisconnected() on an already-closed session.
		// 한글: 연결 종료 또는 에러 — AsyncScope를 경유해 Close()가 먼저 호출된 경우
		//       (Cancel() 완료) OnDisconnected()가 이미 닫힌 세션에서 실행되지 않도록 보장.
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

	// 통계 업데이트 (relaxed 순서로도 충분 — 통계는 정확한 순서 보장이 불필요)
	mTotalBytesReceived.fetch_add(bytesReceived, std::memory_order_relaxed);

	// Dispatch via AsyncScope so that pending tasks are skipped after session Close().
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
				// DataReceived carries raw TCP segment bytes, NOT necessarily
				//          complete application packets. ProcessRawRecv handles packet
				//          assembly internally and invokes OnRecv per complete packet.
				//          Use OnRecv / SetOnRecv for application packet processing;
				//          DataReceived is intended for raw byte monitoring/tracing only.
				// 한글: DataReceived는 완성된 애플리케이션 패킷이 아닌 원시 TCP 세그먼트
				//       바이트를 전달한다. 패킷 조립은 ProcessRawRecv가 내부적으로 처리하며
				//       완성된 패킷마다 OnRecv를 호출한다.
				//       애플리케이션 패킷 처리에는 OnRecv / SetOnRecv를 사용하고,
				//       DataReceived는 원시 바이트 모니터링/트레이싱 전용으로 의도됨.
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
	// 한글: 송신 경로의 0 또는 음수 완료는 원격 종료 또는 OS 수준 에러를 의미.
	//       ProcessErrorCompletion으로 라우팅하여 세션을 정상 종료하고
	//       송신 에러 통계를 업데이트한다. 이런 완료에서 DataSent를 발생시키거나
	//       PostSend를 호출하는 것은 잘못된 동작이다.
	if (bytesSent <= 0)
	{
		ProcessErrorCompletion(session, AsyncIO::AsyncIOType::Send, 0);
		return;
	}

	FireEvent(NetworkEvent::DataSent, session->GetId());

	// 큐에 더 많은 데이터가 있으면 계속 전송
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
	// 한글: disconnect 라우팅 전에 방향별 카운터를 증가.
	//       Send/Recv 에러를 분리 집계하여 인바운드/아웃바운드 경로 장애 진단 가능.
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
	// 한글: 정상적인 원격 연결 종료 시 발생하는 에러 코드는 Warn 레벨 기록 —
	//       운영자에게 불필요한 경보를 주지 않기 위함.
	//       예상치 못한 코드는 실제 문제일 수 있으므로 Error 레벨 기록.
	//       osError == 0은 "다음 I/O 작업 큐 등록 실패" — 하위 에러는
	//       호출자(QueueRecv)에서 이미 기록하므로 Warn 레벨.
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
	// 한글: ProcessRecvCompletion(bytesReceived=0)을 경유하여 disconnect 이벤트를
	//       항상 session->mAsyncScope를 통해 제출.
	//       다른 경로에서 Close()가 동시에 호출된 경우 이미 닫힌 세션에서
	//       OnDisconnected()가 발생하는 이중 이벤트를 방지.
	ProcessRecvCompletion(session, 0, nullptr);
}

} // namespace Network::Core
