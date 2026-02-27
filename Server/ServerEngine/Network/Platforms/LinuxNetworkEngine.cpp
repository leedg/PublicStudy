// English: Linux NetworkEngine implementation
// 한글: Linux NetworkEngine 구현

#ifdef __linux__

#include "LinuxNetworkEngine.h"
#include "../../Utils/Logger.h"
#include "../../Platforms/Linux/EpollAsyncIOProvider.h"
#include "../../Platforms/Linux/IOUringAsyncIOProvider.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

namespace Network::Platforms
{

LinuxNetworkEngine::LinuxNetworkEngine(Mode mode)
	: mMode(mode), mListenSocket(-1), mAcceptBackoffMs(10)
{
	Utils::Logger::Info("LinuxNetworkEngine created with mode: " +
						std::string(mode == Mode::Epoll ? "epoll" : "io_uring"));
}

LinuxNetworkEngine::~LinuxNetworkEngine()
{
	Stop();
}

// =============================================================================
// English: Platform-specific implementation
// 한글: 플랫폼별 구현
// =============================================================================

bool LinuxNetworkEngine::InitializePlatform()
{
	// English: Create AsyncIOProvider based on mode
	// 한글: 모드에 따라 AsyncIOProvider 생성
	if (mMode == Mode::Epoll)
	{
		mProvider = std::make_unique<AsyncIO::Linux::EpollAsyncIOProvider>();
		Utils::Logger::Info("Using epoll backend");
	}
	else // IOUring
	{
		mProvider = std::make_unique<AsyncIO::Linux::IOUringAsyncIOProvider>();
		Utils::Logger::Info("Using io_uring backend");
	}

	// English: Initialize provider
	// 한글: Provider 초기화
	auto error = mProvider->Initialize(
		1024,                         // Queue depth
		mMaxConnections > 0 ? static_cast<size_t>(mMaxConnections) : 128 // Max concurrent
	);

	if (error != AsyncIO::AsyncIOError::Success)
	{
		Utils::Logger::Error("Failed to initialize AsyncIOProvider: " +
							 std::string(mProvider->GetLastError()));
		return false;
	}

	// English: Create listen socket
	// 한글: Listen 소켓 생성
	if (!CreateListenSocket())
	{
		return false;
	}

	return true;
}

void LinuxNetworkEngine::ShutdownPlatform()
{
	// English: Close listen socket
	// 한글: Listen 소켓 닫기
	if (mListenSocket != -1)
	{
		close(mListenSocket);
		mListenSocket = -1;
	}

	// English: Shutdown provider
	// 한글: Provider 종료
	if (mProvider)
	{
		mProvider->Shutdown();
	}

	Utils::Logger::Info("LinuxNetworkEngine platform shutdown complete");
}

bool LinuxNetworkEngine::StartPlatformIO()
{
	// English: Start worker threads for completion processing
	// 한글: 완료 처리를 위한 워커 스레드 시작
	uint32_t workerCount = std::thread::hardware_concurrency();
	if (workerCount == 0)
	{
		workerCount = 4;
	}

	for (uint32_t i = 0; i < workerCount; ++i)
	{
		mWorkerThreads.emplace_back([this]() { this->WorkerThread(); });
	}

	// English: Start accept thread
	// 한글: Accept 스레드 시작
	mAcceptThread = std::thread([this]() { this->AcceptLoop(); });

	Utils::Logger::Info("Started " + std::to_string(workerCount) +
						" worker threads + 1 accept thread");
	return true;
}

void LinuxNetworkEngine::StopPlatformIO()
{
	// English: Stop accept thread
	// 한글: Accept 스레드 중지
	if (mListenSocket != -1)
	{
		close(mListenSocket);
		mListenSocket = -1;
	}

	if (mAcceptThread.joinable())
	{
		mAcceptThread.join();
	}

	// English: Stop worker threads (they check mRunning flag)
	// 한글: 워커 스레드 중지 (mRunning 플래그 확인)
	for (auto &thread : mWorkerThreads)
	{
		if (thread.joinable())
		{
			thread.join();
		}
	}
	mWorkerThreads.clear();

	Utils::Logger::Info("All I/O threads stopped");
}

void LinuxNetworkEngine::AcceptLoop()
{
	Utils::Logger::Info("Accept thread started");

	sockaddr_in clientAddr;
	socklen_t clientAddrSize = sizeof(clientAddr);

	while (mRunning)
	{
		// English: Accept incoming connection
		// 한글: 들어오는 연결 수락
		int clientSocket = accept(
			mListenSocket,
			reinterpret_cast<sockaddr *>(&clientAddr),
			&clientAddrSize);

		if (clientSocket < 0)
		{
			if (errno == EINTR || errno == EBADF)
			{
				// English: Socket closed (shutdown signal)
				// 한글: 소켓 닫힘 (종료 신호)
				break;
			}

			Utils::Logger::Error("Accept failed: " + std::string(strerror(errno)));

			// English: Exponential backoff on error (member var, not static)
			// 한글: 에러 시 지수 백오프 (static 대신 멤버 변수 사용)
			std::this_thread::sleep_for(std::chrono::milliseconds(mAcceptBackoffMs));
			mAcceptBackoffMs = (std::min)(mAcceptBackoffMs * 2, 1000);
			continue;
		}

		// English: Reset backoff on success
		// 한글: 성공 시 백오프 리셋
		mAcceptBackoffMs = 10;

		// English: Set socket to non-blocking mode
		// 한글: 소켓을 논블로킹 모드로 설정
		int flags = fcntl(clientSocket, F_GETFL, 0);
		fcntl(clientSocket, F_SETFL, flags | O_NONBLOCK);

		// English: Create session
		// 한글: 세션 생성
		Core::SessionRef session =
			Core::SessionManager::Instance().CreateSession(clientSocket);
		if (!session)
		{
			close(clientSocket);
			continue;
		}

		// English: Associate client socket with async I/O provider (epoll/io_uring)
		// 한글: 클라이언트 소켓을 비동기 I/O 프로바이더에 연결 (epoll/io_uring)
		auto assocResult = mProvider->AssociateSocket(
			clientSocket,
			static_cast<AsyncIO::RequestContext>(session->GetId()));
		if (assocResult != AsyncIO::AsyncIOError::Success)
		{
			Utils::Logger::Error(
				"Failed to associate socket with async I/O - Session " +
				std::to_string(session->GetId()) + ": " +
				std::string(mProvider->GetLastError()));
			Core::SessionManager::Instance().RemoveSession(session);
			close(clientSocket);
			continue;
		}

		// English: Set async provider so session can queue sends via EPOLLOUT
		// 한글: EPOLLOUT을 통해 세션이 송신을 큐에 넣을 수 있도록 async 공급자 설정
		session->SetAsyncProvider(mProvider.get());

		// English: Update stats (atomic)
		// 한글: 통계 업데이트 (atomic)
		mTotalConnections.fetch_add(1, std::memory_order_relaxed);

		// English: Fire Connected event asynchronously on logic thread
		// 한글: 로직 스레드에서 비동기로 Connected 이벤트 발생
		auto sessionCopy = session;
		mLogicThreadPool.Submit(
			[this, sessionCopy]()
			{
				sessionCopy->OnConnected();
				FireEvent(Core::NetworkEvent::Connected, sessionCopy->GetId());
			});

	// English: Start receiving on this session
	// 한글: 이 세션에서 수신 시작
	if (!QueueRecv(session))
	{
		Utils::Logger::Error("Failed to queue recv - Session " +
							 std::to_string(session->GetId()));
		Core::SessionManager::Instance().RemoveSession(session);
		close(clientSocket);
		continue;
	}

		// English: Log connection
		// 한글: 연결 로깅
		char clientIP[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, sizeof(clientIP));
		Utils::Logger::Info("Client connected - IP: " + std::string(clientIP) +
							":" + std::to_string(ntohs(clientAddr.sin_port)) +
							" (Session " + std::to_string(session->GetId()) + ")");
	}

	Utils::Logger::Info("Accept thread stopped");
}

void LinuxNetworkEngine::ProcessCompletions()
{
	// English: Process completions from AsyncIOProvider
	// 한글: AsyncIOProvider로부터 완료 처리
	AsyncIO::CompletionEntry entries[64];
	int count = mProvider->ProcessCompletions(entries, 64, 100);

	if (count < 0)
	{
		// English: Error occurred
		// 한글: 에러 발생
		Utils::Logger::Error("ProcessCompletions failed: " +
							 std::string(mProvider->GetLastError()));
		return;
	}

	// English: No completions - provider already waited with timeout, just return
	// 한글: 완료 없음 - 프로바이더가 이미 타임아웃으로 대기했으므로 바로 리턴
	if (count == 0)
	{
		return;
	}

	for (int i = 0; i < count; ++i)
	{
		auto &entry = entries[i];

		// English: Get session from context (ConnectionId stored in context)
		// 한글: 컨텍스트에서 세션 가져오기 (ConnectionId가 컨텍스트에 저장됨)
		Utils::ConnectionId connId = static_cast<Utils::ConnectionId>(entry.mContext);
		auto session = Core::SessionManager::Instance().GetSession(connId);

		if (!session)
		{
			// English: Session no longer exists
			// 한글: 세션이 더 이상 존재하지 않음
			continue;
		}

		// English: Check for errors
		// 한글: 에러 확인
		if (entry.mOsError != 0 || entry.mResult <= 0)
		{
			// English: Connection error or closed
			// 한글: 연결 에러 또는 닫힘
			auto sessionCopy = session;
			mLogicThreadPool.Submit(
				[this, sessionCopy]()
				{
					sessionCopy->OnDisconnected();
					FireEvent(Core::NetworkEvent::Disconnected,
							  sessionCopy->GetId());
				});

			Core::SessionManager::Instance().RemoveSession(session);
			continue;
		}

		// English: Process based on I/O type
		// 한글: I/O 타입에 따라 처리
		switch (entry.mType)
		{
		case AsyncIO::AsyncIOType::Recv:
		{
			// English: Get received data from session's recv buffer
			// 한글: 세션의 수신 버퍼에서 받은 데이터 가져오기
			const char *recvBuffer = session->GetRecvBuffer();
			ProcessRecvCompletion(session, entry.mResult, recvBuffer);

			// English: Post next receive
			// 한글: 다음 수신 등록
			QueueRecv(session);
			break;
		}

		case AsyncIO::AsyncIOType::Send:
		{
			ProcessSendCompletion(session, entry.mResult);
			break;
		}

		default:
			break;
		}
	}
}

void LinuxNetworkEngine::WorkerThread()
{
	Utils::Logger::Debug("Worker thread started");

	while (mRunning)
	{
		// English: Process completions in loop
		// 한글: 루프에서 완료 처리
		ProcessCompletions();
	}

	Utils::Logger::Debug("Worker thread stopped");
}

bool LinuxNetworkEngine::QueueRecv(const Core::SessionRef &session)
{
	if (!session || !mProvider)
	{
		return false;
	}

	auto error = mProvider->RecvAsync(
		session->GetSocket(),
		session->GetRecvBuffer(),
		session->GetRecvBufferSize(),
		static_cast<AsyncIO::RequestContext>(session->GetId()));

	if (error != AsyncIO::AsyncIOError::Success)
	{
		Utils::Logger::Error("RecvAsync failed: " + std::string(mProvider->GetLastError()));
		return false;
	}

	return true;
}

// =============================================================================
// English: Private helper methods
// 한글: Private 헬퍼 메서드
// =============================================================================

bool LinuxNetworkEngine::CreateListenSocket()
{
	// English: Create socket
	// 한글: 소켓 생성
	mListenSocket = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

	if (mListenSocket < 0)
	{
		Utils::Logger::Error("Failed to create listen socket: " +
							 std::string(strerror(errno)));
		return false;
	}

	// English: Set socket options
	// 한글: 소켓 옵션 설정
	int reuseAddr = 1;
	if (setsockopt(mListenSocket, SOL_SOCKET, SO_REUSEADDR,
				   &reuseAddr, sizeof(reuseAddr)) < 0)
	{
		Utils::Logger::Warn("Failed to set SO_REUSEADDR");
	}

	// English: Bind socket
	// 한글: 소켓 바인드
	sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = INADDR_ANY;
	serverAddr.sin_port = htons(mPort);

	if (bind(mListenSocket, reinterpret_cast<sockaddr *>(&serverAddr),
			 sizeof(serverAddr)) < 0)
	{
		Utils::Logger::Error("Bind failed on port " + std::to_string(mPort) +
							 ": " + std::string(strerror(errno)));
		close(mListenSocket);
		mListenSocket = -1;
		return false;
	}

	// English: Listen for connections
	// 한글: 연결 대기
	if (listen(mListenSocket, SOMAXCONN) < 0)
	{
		Utils::Logger::Error("Listen failed: " +
							 std::string(strerror(errno)));
		close(mListenSocket);
		mListenSocket = -1;
		return false;
	}

	Utils::Logger::Info("Listen socket created and bound to port " +
						std::to_string(mPort));
	return true;
}

} // namespace Network::Platforms

#endif // __linux__
