// English: Windows NetworkEngine implementation
// 한글: Windows NetworkEngine 구현

#ifdef _WIN32

#include "WindowsNetworkEngine.h"
#include "../../Utils/Logger.h"
#include "../../Platforms/Windows/IocpAsyncIOProvider.h"
#include "../../Platforms/Windows/RIOAsyncIOProvider.h"
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

namespace Network::Platforms
{

WindowsNetworkEngine::WindowsNetworkEngine(Mode mode)
	: mMode(mode), mListenSocket(INVALID_SOCKET)
{
	Utils::Logger::Info("WindowsNetworkEngine created with mode: " +
						std::string(mode == Mode::IOCP ? "IOCP" : "RIO"));
}

WindowsNetworkEngine::~WindowsNetworkEngine()
{
	Stop();
}

// =============================================================================
// English: Platform-specific implementation
// 한글: 플랫폼별 구현
// =============================================================================

bool WindowsNetworkEngine::InitializePlatform()
{
	// English: Initialize Winsock
	// 한글: Winsock 초기화
	if (!InitializeWinsock())
	{
		return false;
	}

	// English: Create AsyncIOProvider based on mode
	// 한글: 모드에 따라 AsyncIOProvider 생성
	if (mMode == Mode::IOCP)
	{
		mProvider = std::make_unique<AsyncIO::Windows::IocpAsyncIOProvider>();
		Utils::Logger::Info("Using IOCP backend");
	}
	else // RIO
	{
		mProvider = std::make_unique<AsyncIO::Windows::RIOAsyncIOProvider>();
		Utils::Logger::Info("Using RIO backend");
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

void WindowsNetworkEngine::ShutdownPlatform()
{
	// English: Close listen socket
	// 한글: Listen 소켓 닫기
	if (mListenSocket != INVALID_SOCKET)
	{
		closesocket(mListenSocket);
		mListenSocket = INVALID_SOCKET;
	}

	// English: Shutdown provider
	// 한글: Provider 종료
	if (mProvider)
	{
		mProvider->Shutdown();
	}

	// English: Cleanup Winsock
	// 한글: Winsock 정리
	WSACleanup();

	Utils::Logger::Info("WindowsNetworkEngine platform shutdown complete");
}

bool WindowsNetworkEngine::StartPlatformIO()
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

void WindowsNetworkEngine::StopPlatformIO()
{
	// English: Stop accept thread
	// 한글: Accept 스레드 중지
	if (mListenSocket != INVALID_SOCKET)
	{
		closesocket(mListenSocket);
		mListenSocket = INVALID_SOCKET;
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

void WindowsNetworkEngine::AcceptLoop()
{
	Utils::Logger::Info("Accept thread started");

	sockaddr_in clientAddr;
	int clientAddrSize = sizeof(clientAddr);

	while (mRunning)
	{
		// English: Accept incoming connection
		// 한글: 들어오는 연결 수락
		SOCKET clientSocket = accept(
			mListenSocket,
			reinterpret_cast<sockaddr *>(&clientAddr),
			&clientAddrSize);

		if (clientSocket == INVALID_SOCKET)
		{
			int error = WSAGetLastError();
			if (error == WSAEINTR || error == WSAENOTSOCK)
			{
				// English: Socket closed (shutdown signal)
				// 한글: 소켓 닫힘 (종료 신호)
				break;
			}

			Utils::Logger::Error("Accept failed: " + std::to_string(error));

			// English: Exponential backoff on error
			// 한글: 에러 시 지수 백오프
			static int backoffMs = 10;
			std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs));
			backoffMs = (std::min)(backoffMs * 2, 1000);
			continue;
		}

		// English: Reset backoff on success
		// 한글: 성공 시 백오프 리셋
		static int backoffMs = 10;

		// English: Create session
		// 한글: 세션 생성
		Core::SessionRef session =
			Core::SessionManager::Instance().CreateSession(clientSocket);
		if (!session)
		{
			closesocket(clientSocket);
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
				FireEvent(Core::NetworkEvent::Connected, sessionCopy->GetId());
			});

		// English: Start receiving on this session
		// 한글: 이 세션에서 수신 시작
		session->PostRecv();

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

void WindowsNetworkEngine::ProcessCompletions()
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
			const char *recvBuffer = session->GetRecvContext().buffer;
			ProcessRecvCompletion(session, entry.mResult, recvBuffer);

			// English: Post next receive
			// 한글: 다음 수신 등록
			session->PostRecv();
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

void WindowsNetworkEngine::WorkerThread()
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

// =============================================================================
// English: Private helper methods
// 한글: Private 헬퍼 메서드
// =============================================================================

bool WindowsNetworkEngine::InitializeWinsock()
{
	WSADATA wsaData;
	int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (result != 0)
	{
		Utils::Logger::Error("WSAStartup failed - Error: " +
							 std::to_string(result));
		return false;
	}

	Utils::Logger::Info("Winsock initialized (version 2.2)");
	return true;
}

bool WindowsNetworkEngine::CreateListenSocket()
{
	// English: Create socket
	// 한글: 소켓 생성
	mListenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0,
								  WSA_FLAG_OVERLAPPED);

	if (mListenSocket == INVALID_SOCKET)
	{
		Utils::Logger::Error("Failed to create listen socket: " +
							 std::to_string(WSAGetLastError()));
		return false;
	}

	// English: Set socket options
	// 한글: 소켓 옵션 설정
	BOOL reuseAddr = TRUE;
	if (setsockopt(mListenSocket, SOL_SOCKET, SO_REUSEADDR,
				   reinterpret_cast<const char *>(&reuseAddr),
				   sizeof(reuseAddr)) == SOCKET_ERROR)
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
			 sizeof(serverAddr)) == SOCKET_ERROR)
	{
		Utils::Logger::Error("Bind failed on port " + std::to_string(mPort) +
							 ": " + std::to_string(WSAGetLastError()));
		closesocket(mListenSocket);
		mListenSocket = INVALID_SOCKET;
		return false;
	}

	// English: Listen for connections
	// 한글: 연결 대기
	if (listen(mListenSocket, SOMAXCONN) == SOCKET_ERROR)
	{
		Utils::Logger::Error("Listen failed: " +
							 std::to_string(WSAGetLastError()));
		closesocket(mListenSocket);
		mListenSocket = INVALID_SOCKET;
		return false;
	}

	Utils::Logger::Info("Listen socket created and bound to port " +
						std::to_string(mPort));
	return true;
}

} // namespace Network::Platforms

#endif // _WIN32
