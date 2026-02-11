// English: Base NetworkEngine implementation
// 한글: 기본 NetworkEngine 구현

#include "BaseNetworkEngine.h"
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

	// English: Call platform-specific initialization
	// 한글: 플랫폼별 초기화 호출
	if (!InitializePlatform())
	{
		Utils::Logger::Error("Platform initialization failed");
		return false;
	}

	mInitialized = true;
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

	mRunning = true;

	// English: Start platform-specific I/O
	// 한글: 플랫폼별 I/O 시작
	if (!StartPlatformIO())
	{
		Utils::Logger::Error("Failed to start platform I/O");
		mRunning = false;
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

	mRunning = false;

	// English: Stop platform-specific I/O
	// 한글: 플랫폼별 I/O 중지
	StopPlatformIO();

	// English: Close all sessions
	// 한글: 모든 세션 종료
	SessionManager::Instance().CloseAllSessions();

	// English: Shutdown platform resources
	// 한글: 플랫폼 리소스 종료
	ShutdownPlatform();

	mInitialized = false;
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

	// English: Call OnDisconnected callback
	// 한글: OnDisconnected 콜백 호출
	session->OnDisconnected();

	// English: RemoveSession will close the session internally
	// 한글: RemoveSession이 내부적으로 세션을 닫음
	SessionManager::Instance().RemoveSession(session);

	// English: Fire disconnection event
	// 한글: 연결 해제 이벤트 발생
	FireEvent(NetworkEvent::Disconnected, connectionId);
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
	std::lock_guard<std::mutex> lock(mCallbackMutex);
	auto it = mCallbacks.find(eventType);
	if (it == mCallbacks.end())
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
	it->second(eventData);
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
	auto dataCopy = std::make_unique<char[]>(bytesReceived);
	std::memcpy(dataCopy.get(), data, bytesReceived);

	if (!mLogicThreadPool.Submit(
		[this, sessionCopy, dataPtr = dataCopy.release(), bytesReceived]()
		{
			std::unique_ptr<char[]> dataHolder(dataPtr);
			sessionCopy->ProcessRawRecv(dataPtr, bytesReceived);
			FireEvent(NetworkEvent::DataReceived, sessionCopy->GetId(),
					  reinterpret_cast<const uint8_t *>(dataPtr), bytesReceived);
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
