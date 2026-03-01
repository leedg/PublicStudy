// English: Base NetworkEngine implementation
// 한글: 기본 NetworkEngine 구현

#include "BaseNetworkEngine.h"
#include "SessionPool.h"
#include "../../Utils/Logger.h"
#include "../../Utils/Timer.h"

namespace Network::Core
{

BaseNetworkEngine::BaseNetworkEngine()
	: mPort(0), mMaxConnections(0), mRunning(false), mInitialized(false),
	  mLogicThreadPool(4, MAX_LOGIC_QUEUE_DEPTH)
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

	// English: Stop platform-specific I/O
	// 한글: 플랫폼별 I/O 중지
	StopPlatformIO();

	// English: Close all sessions
	// 한글: 모든 세션 종료
	SessionManager::Instance().CloseAllSessions();

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

	// English: Session::Send is void - check IsConnected() after to detect immediate failures
	// 한글: Session::Send는 void이므로 즉각적인 실패 감지를 위해 이후 IsConnected() 확인
	session->Send(data, static_cast<uint32_t>(size));

	if (!session->IsConnected())
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

	// English: Submit OnDisconnected + FireEvent to the logic thread pool.
	//          This matches ProcessRecvCompletion's disconnect path so that all
	//          OnDisconnected callbacks always execute on the same thread pool —
	//          callers never need to worry about which thread invoked CloseConnection.
	//          The session shared_ptr keeps the object alive until the task runs.
	// 한글: OnDisconnected + FireEvent를 로직 스레드풀에 제출.
	//       ProcessRecvCompletion의 연결 종료 경로와 동일하게 처리하여
	//       OnDisconnected 콜백이 항상 같은 스레드풀에서 실행되도록 보장.
	//       세션 shared_ptr이 작업 실행 전까지 객체를 살아있게 유지.
	auto sessionCopy = session;
	mLogicThreadPool.Submit(
		[this, sessionCopy, connectionId]()
		{
			sessionCopy->OnDisconnected();
			FireEvent(NetworkEvent::Disconnected, connectionId);
		});

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
		// English: Connection closed or error
		// 한글: 연결 종료 또는 에러
		auto sessionCopy = session;
		mLogicThreadPool.Submit(
			[this, sessionCopy]()
			{
				sessionCopy->OnDisconnected();
				FireEvent(NetworkEvent::Disconnected, sessionCopy->GetId());
			});

		SessionManager::Instance().RemoveSession(session);
		return;
	}

	// English: Update stats (atomic, no lock needed)
	// 한글: 통계 업데이트 (atomic, 락 불필요)
	mTotalBytesReceived.fetch_add(bytesReceived, std::memory_order_relaxed);

	// English: Process on logic thread
	// 한글: 로직 스레드에서 처리
	auto sessionCopy = session;
	auto dataCopy = std::make_shared<std::vector<char>>(
		static_cast<size_t>(bytesReceived));
	std::memcpy(dataCopy->data(), data, static_cast<size_t>(bytesReceived));

	if (!mLogicThreadPool.Submit(
		[this, sessionCopy, dataCopy, bytesReceived]()
		{
			const char *recvData = dataCopy->data();
			sessionCopy->ProcessRawRecv(recvData, bytesReceived);
			FireEvent(NetworkEvent::DataReceived, sessionCopy->GetId(),
					  reinterpret_cast<const uint8_t *>(recvData), bytesReceived);
		}))
	{
		Utils::Logger::Warn("Logic queue full - recv dropped, disconnecting Session: " +
							std::to_string(session->GetId()));
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
