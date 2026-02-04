// English: IOCPNetworkEngine implementation
// 한글: IOCPNetworkEngine 구현

#include "IOCPNetworkEngine.h"
#include <iostream>
#include <sstream>
#include <thread>
#include <algorithm>

#ifdef _WIN32
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")
#endif

namespace Network::Core
{

IOCPNetworkEngine::IOCPNetworkEngine()
	: mListenSocket(
#ifdef _WIN32
		  INVALID_SOCKET
#else
		  -1
#endif
		  ),
		  mPort(0), mMaxConnections(0), mRunning(false), mInitialized(false),
		  mLogicThreadPool(4)
#ifdef _WIN32
		  ,
		  mIOCP(nullptr)
#endif
{
	std::memset(&mStats, 0, sizeof(mStats));
}

IOCPNetworkEngine::~IOCPNetworkEngine() { Stop(); }

// =============================================================================
// English: INetworkEngine interface
// 한글: INetworkEngine 인터페이스
// =============================================================================

bool IOCPNetworkEngine::Initialize(size_t maxConnections, uint16_t port)
{
	if (mInitialized)
	{
		Utils::Logger::Warn("IOCPNetworkEngine already initialized");
		return false;
	}

	mPort = port;
	mMaxConnections = maxConnections;
	mStats.startTime = Utils::Timer::GetCurrentTimestamp();

	if (!InitializeWinsock())
	{
		return false;
	}

	if (!CreateListenSocket())
	{
		return false;
	}

	if (!CreateIOCP())
	{
		return false;
	}

	mInitialized = true;
	Utils::Logger::Info("IOCPNetworkEngine initialized on port " +
						std::to_string(mPort));
	return true;
}

bool IOCPNetworkEngine::Start()
{
	if (!mInitialized)
	{
		Utils::Logger::Error("IOCPNetworkEngine not initialized");
		return false;
	}

	if (mRunning)
	{
		Utils::Logger::Warn("IOCPNetworkEngine already running");
		return false;
	}

	mRunning = true;

	// English: Start IOCP worker threads (CPU core count)
	// 한글: IOCP 워커 스레드 시작 (CPU 코어 수)
	uint32_t workerCount = std::thread::hardware_concurrency();
	if (workerCount == 0)
	{
		workerCount = 4;
	}

	for (uint32_t i = 0; i < workerCount; ++i)
	{
		// Use lambda to avoid member-function overload issues on some toolsets
		mWorkerThreads.emplace_back([this]() { this->WorkerThread(); });
	}

	// English: Start accept thread
	// 한글: Accept 스레드 시작
	// Note: Full thread implementation deferred to platform-specific code

	Utils::Logger::Info("IOCPNetworkEngine started - Workers: " +
						std::to_string(workerCount));
	return true;
}

void IOCPNetworkEngine::Stop()
{
	if (!mRunning)
	{
		return;
	}

	mRunning = false;

	// English: Close listen socket to unblock accept
	// 한글: accept 블로킹 해제를 위해 listen 소켓 닫기
#ifdef _WIN32
	if (mListenSocket != INVALID_SOCKET)
	{
		closesocket(mListenSocket);
		mListenSocket = INVALID_SOCKET;
	}
#endif

	if (mAcceptThread.joinable())
	{
		mAcceptThread.join();
	}

	// English: Post exit signals to IOCP workers
	// 한글: IOCP 워커에 종료 신호
#ifdef _WIN32
	if (mIOCP)
	{
		for (size_t i = 0; i < mWorkerThreads.size(); ++i)
		{
			PostQueuedCompletionStatus(mIOCP, 0, 0, nullptr);
		}
	}
#endif

	for (auto &thread : mWorkerThreads)
	{
		if (thread.joinable())
		{
			thread.join();
		}
	}
	mWorkerThreads.clear();

	// English: Close all sessions
	// 한글: 모든 세션 종료
	SessionManager::Instance().CloseAllSessions();

#ifdef _WIN32
	if (mIOCP)
	{
		CloseHandle(mIOCP);
		mIOCP = nullptr;
	}

	WSACleanup();
#endif

	mInitialized = false;
	Utils::Logger::Info("IOCPNetworkEngine stopped");
}

bool IOCPNetworkEngine::IsRunning() const { return mRunning; }

bool IOCPNetworkEngine::RegisterEventCallback(NetworkEvent eventType,
												  NetworkEventCallback callback)
{
	std::lock_guard<std::mutex> lock(mCallbackMutex);
	mCallbacks[eventType] = std::move(callback);
	return true;
}

void IOCPNetworkEngine::UnregisterEventCallback(NetworkEvent eventType)
{
	std::lock_guard<std::mutex> lock(mCallbackMutex);
	mCallbacks.erase(eventType);
}

bool IOCPNetworkEngine::SendData(Utils::ConnectionId connectionId,
								 const void *data, size_t size)
{
	auto session = SessionManager::Instance().GetSession(connectionId);
	if (!session || !session->IsConnected())
	{
		return false;
	}

	session->Send(data, static_cast<uint32_t>(size));

	{
		std::lock_guard<std::mutex> lock(mStatsMutex);
		mStats.totalBytesSent += size;
	}

	return true;
}

void IOCPNetworkEngine::CloseConnection(Utils::ConnectionId connectionId)
{
	auto session = SessionManager::Instance().GetSession(connectionId);
	if (session)
	{
		session->Close();
		session->OnDisconnected();
		SessionManager::Instance().RemoveSession(connectionId);

		FireEvent(NetworkEvent::Disconnected, connectionId);
	}
}

std::string
IOCPNetworkEngine::GetConnectionInfo(Utils::ConnectionId connectionId) const
{
	auto session = SessionManager::Instance().GetSession(connectionId);
	if (!session)
	{
		return "";
	}

	return "Session[" + std::to_string(connectionId) +
			   "] State=" + std::to_string(static_cast<int>(session->GetState()));
}

INetworkEngine::Statistics IOCPNetworkEngine::GetStatistics() const
{
	std::lock_guard<std::mutex> lock(mStatsMutex);
	INetworkEngine::Statistics stats = {};
	// Copy fields explicitly to ensure matching type
	stats.totalConnections = mStats.totalConnections;
	stats.activeConnections = SessionManager::Instance().GetSessionCount();
	stats.totalBytesSent = mStats.totalBytesSent;
	stats.totalBytesReceived = mStats.totalBytesReceived;
	stats.totalErrors = mStats.totalErrors;
	stats.startTime = mStats.startTime;
	return stats;
}

// =============================================================================
// English: Internal initialization
// 한글: 내부 초기화
// =============================================================================

bool IOCPNetworkEngine::InitializeWinsock()
{
#ifdef _WIN32
	WSADATA wsaData;
	int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (result != 0)
	{
		Utils::Logger::Error("WSAStartup failed - Error: " +
							 std::to_string(result));
		return false;
	}
	Utils::Logger::Info("Winsock initialized");
	return true;
#else
	return true;
#endif
}

bool IOCPNetworkEngine::CreateListenSocket()
{
#ifdef _WIN32
	mListenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0,
								  WSA_FLAG_OVERLAPPED);

	if (mListenSocket == INVALID_SOCKET)
	{
		Utils::Logger::Error("Failed to create listen socket");
		return false;
	}

	// English: SO_REUSEADDR
	// 한글: 소켓 재사용 설정
	int opt = 1;
	setsockopt(mListenSocket, SOL_SOCKET, SO_REUSEADDR,
				   reinterpret_cast<char *>(&opt), sizeof(opt));

	sockaddr_in serverAddr = {};
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = INADDR_ANY;
	serverAddr.sin_port = htons(mPort);

	if (bind(mListenSocket, reinterpret_cast<sockaddr *>(&serverAddr),
			 sizeof(serverAddr)) == SOCKET_ERROR)
	{
		Utils::Logger::Error("Bind failed - Error: " +
							 std::to_string(WSAGetLastError()));
		return false;
	}

	if (listen(mListenSocket, SOMAXCONN) == SOCKET_ERROR)
	{
		Utils::Logger::Error("Listen failed - Error: " +
							 std::to_string(WSAGetLastError()));
		return false;
	}

	Utils::Logger::Info("Listen socket created on port " +
						std::to_string(mPort));
	return true;
#else
	Utils::Logger::Error("Listen socket not implemented for this platform");
	return false;
#endif
}

bool IOCPNetworkEngine::CreateIOCP()
{
#ifdef _WIN32
	mIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
	if (mIOCP == nullptr)
	{
		Utils::Logger::Error("Failed to create IOCP");
		return false;
	}
	Utils::Logger::Info("IOCP created");
	return true;
#else
	return false;
#endif
}

// =============================================================================
// English: Thread functions
// 한글: 스레드 함수
// =============================================================================

void IOCPNetworkEngine::AcceptThread()
{
	Utils::Logger::Info("Accept thread started");

	// English: Exponential backoff for accept failures
	// 한글: Accept 실패 시 지수 백오프
	uint32_t failureCount = 0;
	constexpr uint32_t maxBackoffMs = 1000;

	while (mRunning)
	{
#ifdef _WIN32
		sockaddr_in clientAddr = {};
		int addrLen = sizeof(clientAddr);

		SocketHandle clientSocket = accept(
			mListenSocket, reinterpret_cast<sockaddr *>(&clientAddr), &addrLen);

		if (clientSocket == INVALID_SOCKET)
		{
			if (mRunning)
			{
				Utils::Logger::Warn("Accept failed - Error: " +
									std::to_string(WSAGetLastError()));
				
				// English: Exponential backoff to prevent CPU spinning
				// 한글: CPU 과부하 방지를 위한 지수 백오프
				failureCount++;
				uint32_t backoffMs = std::min(static_cast<uint32_t>(1 << failureCount), maxBackoffMs);
				std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs));
			}
			continue;
		}

		// English: Reset failure count on success
		// 한글: 성공 시 실패 카운터 초기화
		failureCount = 0;

		// English: Create session via SessionManager
		// 한글: SessionManager를 통해 세션 생성
		SessionRef session =
			SessionManager::Instance().CreateSession(clientSocket);
		if (!session)
		{
			closesocket(clientSocket);
			continue;
		}

		// English: Associate socket with IOCP
		// 한글: 소켓을 IOCP에 등록
		if (CreateIoCompletionPort(
				reinterpret_cast<HANDLE>(clientSocket), mIOCP,
				static_cast<ULONG_PTR>(session->GetId()), 0) == nullptr)
		{
			Utils::Logger::Error("Failed to associate socket with IOCP");
			SessionManager::Instance().RemoveSession(session);
			continue;
		}

		// English: Update stats
		// 한글: 통계 업데이트
		{
			std::lock_guard<std::mutex> lock(mStatsMutex);
			mStats.totalConnections++;
		}

		// English: Fire Connected event asynchronously on logic thread
		// 한글: 로직 스레드에서 비동기로 Connected 이벤트 발생
		auto sessionCopy = session;
		mLogicThreadPool.Submit(
			[this, sessionCopy]()
			{
				sessionCopy->OnConnected();
				FireEvent(NetworkEvent::Connected, sessionCopy->GetId());
			});

		// English: Start receiving
		// 한글: 수신 시작
		session->PostRecv();

		char clientIP[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, sizeof(clientIP));

		Utils::Logger::Info("Client connected - IP: " + std::string(clientIP) +
							":" + std::to_string(ntohs(clientAddr.sin_port)));
#endif
	}

	Utils::Logger::Info("Accept thread stopped");
}

void IOCPNetworkEngine::WorkerThread()
{
#ifdef _WIN32
	while (mRunning)
	{
		DWORD bytesTransferred = 0;
		ULONG_PTR completionKey = 0;
		OVERLAPPED *overlapped = nullptr;

		BOOL result = GetQueuedCompletionStatus(
			mIOCP, &bytesTransferred, &completionKey, &overlapped, INFINITE);

		// English: Exit signal (nullptr overlapped, 0 key)
		// 한글: 종료 신호
		if (overlapped == nullptr)
		{
			break;
		}

		Utils::ConnectionId connId =
			static_cast<Utils::ConnectionId>(completionKey);
		SessionRef session = SessionManager::Instance().GetSession(connId);

		if (!session)
		{
			continue;
		}

		IOContext *ioContext = static_cast<IOContext *>(overlapped);

		if (!result || bytesTransferred == 0)
		{
			// English: Connection closed
			// 한글: 연결 종료
			auto sessionCopy = session;
			mLogicThreadPool.Submit(
				[this, sessionCopy]()
				{
					sessionCopy->OnDisconnected();
					FireEvent(NetworkEvent::Disconnected, sessionCopy->GetId());
				});

			session->Close();
			SessionManager::Instance().RemoveSession(session);
			continue;
		}

		// English: Process IO completion
		// 한글: IO 완료 처리
		switch (ioContext->type)
		{
		case IOType::Recv:
			ProcessRecvCompletion(session, bytesTransferred);
			break;

		case IOType::Send:
			ProcessSendCompletion(session, bytesTransferred);
			break;

		default:
			break;
		}
	}
#endif
}

void IOCPNetworkEngine::ProcessRecvCompletion(SessionRef session,
												  uint32_t bytesTransferred)
{
	if (!session || !session->IsConnected())
	{
		return;
	}

	// English: Update stats
	// 한글: 통계 업데이트
	{
		std::lock_guard<std::mutex> lock(mStatsMutex);
		mStats.totalBytesReceived += bytesTransferred;
	}

#ifdef _WIN32
	// English: Get pointer to recv buffer directly (avoid copy)
	// 한글: 수신 버퍼에 직접 접근 (복사 방지)
	const char* recvBuffer = session->GetRecvContext().buffer;
	
	// English: Process on logic thread with buffer pointer
	// 한글: 버퍼 포인터로 로직 스레드에서 처리
	auto sessionCopy = session;
	
	// English: Only copy data if we need to keep it after PostRecv
	// 한글: PostRecv 이후에도 데이터를 유지해야 하는 경우에만 복사
	std::vector<char> dataCopy(recvBuffer, recvBuffer + bytesTransferred);
	
	mLogicThreadPool.Submit(
		[this, sessionCopy, data = std::move(dataCopy)]()
		{
			sessionCopy->OnRecv(data.data(),
								static_cast<uint32_t>(data.size()));

			// English: Fire DataReceived event
			// 한글: DataReceived 이벤트 발생
			FireEvent(NetworkEvent::DataReceived, sessionCopy->GetId(),
						  reinterpret_cast<const uint8_t *>(data.data()),
						  data.size());
		});
#endif

	// English: Post next receive
	// 한글: 다음 수신 등록
	session->PostRecv();
}

void IOCPNetworkEngine::ProcessSendCompletion(SessionRef session,
												  uint32_t bytesTransferred)
{
	if (!session || !session->IsConnected())
	{
		return;
	}

	// English: Send completion is handled by Session internally
	// 한글: 전송 완료는 Session 내부에서 처리
	FireEvent(NetworkEvent::DataSent, session->GetId());
}

void IOCPNetworkEngine::FireEvent(NetworkEvent eventType,
								  Utils::ConnectionId connId,
								  const uint8_t *data, size_t dataSize,
								  OSError errorCode)
{
	std::lock_guard<std::mutex> lock(mCallbackMutex);

	auto it = mCallbacks.find(eventType);
	if (it != mCallbacks.end())
	{
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

		it->second(eventData);
	}
}

// =============================================================================
// English: Factory function implementation
// 한글: 팩토리 함수 구현
// =============================================================================

std::unique_ptr<INetworkEngine>
CreateNetworkEngine(const std::string &engineType)
{
#ifdef _WIN32
	return std::unique_ptr<INetworkEngine>(new IOCPNetworkEngine());
#else
	Utils::Logger::Error("No network engine available for this platform");
	return nullptr;
#endif
}

std::vector<std::string> GetAvailableEngineTypes()
{
	std::vector<std::string> types;
#ifdef _WIN32
	types.push_back("iocp");
#endif
	return types;
}

} // namespace Network::Core
