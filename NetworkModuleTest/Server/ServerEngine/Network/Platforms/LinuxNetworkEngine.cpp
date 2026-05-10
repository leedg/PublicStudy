// Linux NetworkEngine 구현

#ifdef __linux__

#include "LinuxNetworkEngine.h"
#include "../../Utils/Logger.h"
#include "../../Platforms/Linux/EpollAsyncIOProvider.h"
#if defined(HAVE_IO_URING) || defined(HAVE_LIBURING)
#include "../../Platforms/Linux/IOUringAsyncIOProvider.h"
#endif
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
// 플랫폼별 구현 (BaseNetworkEngine 순수 가상 재정의)
// =============================================================================

bool LinuxNetworkEngine::InitializePlatform()
{
	// 모드에 따라 AsyncIOProvider를 생성한다.
	// IOUring 선택 시: 빌드 시스템(CMake)이 HAVE_IO_URING 또는 HAVE_LIBURING를 정의한
	// 경우에만 사용 가능하며, 정의되지 않았거나 런타임 초기화 실패 시 epoll로 폴백.
	if (mMode == Mode::Epoll)
	{
		mProvider = std::make_shared<AsyncIO::Linux::EpollAsyncIOProvider>();
		Utils::Logger::Info("Using epoll backend");
	}
	else // IOUring
	{
#if defined(HAVE_IO_URING) || defined(HAVE_LIBURING)
		mProvider = std::make_shared<AsyncIO::Linux::IOUringAsyncIOProvider>();
		Utils::Logger::Info("Using io_uring backend");
#else
		// 컴파일 타임에 io_uring 미지원 (HAVE_LIBURING 미정의) — epoll로 폴백.
		Utils::Logger::Warn("io_uring not available (HAVE_LIBURING not defined), falling back to epoll");
		mMode     = Mode::Epoll;
		mProvider = std::make_shared<AsyncIO::Linux::EpollAsyncIOProvider>();
		Utils::Logger::Info("Using epoll backend");
#endif
	}

	// AsyncIOProvider 초기화
	auto error = mProvider->Initialize(
		1024,                         // Queue depth
		mMaxConnections > 0 ? static_cast<size_t>(mMaxConnections) : 128 // Max concurrent
	);

	if (error != AsyncIO::AsyncIOError::Success)
	{
		if (mMode == Mode::IOUring)
		{
			Utils::Logger::Warn("io_uring init failed (" +
								std::string(mProvider->GetLastError()) +
								"), falling back to epoll");
			mProvider = std::make_shared<AsyncIO::Linux::EpollAsyncIOProvider>();
			mMode     = Mode::Epoll;
			error     = mProvider->Initialize(
				1024,
				mMaxConnections > 0 ? static_cast<size_t>(mMaxConnections) : 128);
		}
		if (error != AsyncIO::AsyncIOError::Success)
		{
			Utils::Logger::Error("Failed to initialize AsyncIOProvider: " +
								 std::string(mProvider->GetLastError()));
			return false;
		}
	}

	// listen 소켓 생성
	if (!CreateListenSocket())
	{
		return false;
	}

	return true;
}

void LinuxNetworkEngine::ShutdownPlatform()
{
	// listen 소켓 닫기
	if (mListenSocket != -1)
	{
		close(mListenSocket);
		mListenSocket = -1;
	}

	// AsyncIOProvider 종료
	if (mProvider)
	{
		mProvider->Shutdown();
	}

	Utils::Logger::Info("LinuxNetworkEngine platform shutdown complete");
}

bool LinuxNetworkEngine::StartPlatformIO()
{
	// 완료 처리 워커 스레드 시작 (hardware_concurrency개)
	uint32_t workerCount = std::thread::hardware_concurrency();
	if (workerCount == 0)
	{
		workerCount = 4;
	}

	for (uint32_t i = 0; i < workerCount; ++i)
	{
		mWorkerThreads.emplace_back([this]() { this->WorkerThread(); });
	}

	// accept 전담 스레드 시작
	mAcceptThread = std::thread([this]() { this->AcceptLoop(); });

	Utils::Logger::Info("Started " + std::to_string(workerCount) +
						" worker threads + 1 accept thread");
	return true;
}

void LinuxNetworkEngine::StopPlatformIO()
{
	// listen 소켓을 닫아 accept 스레드를 종료한다
	if (mListenSocket != -1)
	{
		close(mListenSocket);
		mListenSocket = -1;
	}

	if (mAcceptThread.joinable())
	{
		mAcceptThread.join();
	}

	// 워커 스레드 종료 (mRunning 플래그를 모니터링하다가 루프 탈출)
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
		// 연결 수락
		int clientSocket = accept(
			mListenSocket,
			reinterpret_cast<sockaddr *>(&clientAddr),
			&clientAddrSize);

		if (clientSocket < 0)
		{
			if (errno == EINTR || errno == EBADF)
			{
				// 소켓이 닫힌 상태 — Shutdown() 신호
				break;
			}

			if (errno == EAGAIN || errno == EWOULDBLOCK)
			{
				// 논블로킹 listen 소켓에서 대기 중인 연결이 없음 — 오류가 아니므로 짧게 양보.
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				continue;
			}

			if (errno == EMFILE || errno == ENFILE || errno == ENOMEM)
			{
				// 시스템 리소스 고갈(EMFILE/ENFILE/ENOMEM) — 더 길게 대기 후 재시도.
				Utils::Logger::Error("Accept resource exhaustion (" +
									 std::string(strerror(errno)) +
									 ") - sleeping 5 s");
				std::this_thread::sleep_for(std::chrono::seconds(5));
				continue;
			}

			Utils::Logger::Error("Accept failed: " + std::string(strerror(errno)));

			// 에러 시 지수 백오프 (static 대신 멤버 변수로 재진입/복수 인스턴스 버그 방지)
			std::this_thread::sleep_for(std::chrono::milliseconds(mAcceptBackoffMs));
			mAcceptBackoffMs = (std::min)(mAcceptBackoffMs * 2, 1000);
			continue;
		}

		// 연결 수락 성공 — 백오프 초기화
		mAcceptBackoffMs = 10;

		// 클라이언트 소켓을 논블로킹 모드로 설정 (epoll/io_uring 등록 전에 수행)
		int flags = fcntl(clientSocket, F_GETFL, 0);
		if (flags != -1)
		{
			fcntl(clientSocket, F_SETFL, flags | O_NONBLOCK);
		}

		// 세션 생성
		Core::SessionRef session =
			Core::SessionManager::Instance().CreateSession(clientSocket);
		if (!session)
		{
			close(clientSocket);
			continue;
		}

		// 클라이언트 소켓을 비동기 I/O 프로바이더(epoll/io_uring)에 등록
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
			// Session이 소켓을 소유하므로 RemoveSession → pool deleter가 Close()를 호출한다.
			// 여기서 close(clientSocket)를 직접 호출하면 이중 닫기 / fd 재사용 경합이 발생한다.
			continue;
		}

		// 세션에 async 프로바이더를 연결하여 EPOLLOUT 경유 송신 큐잉을 활성화
		session->SetAsyncProvider(mProvider);

		// 연결 수 통계 업데이트 (memory_order_relaxed)
		mTotalConnections.fetch_add(1, std::memory_order_relaxed);

		// KeyedDispatcher를 통해 Connected 이벤트를 로직 스레드에 비동기 디스패치
		auto sessionCopy = session;
		mLogicDispatcher.Dispatch(sessionCopy->GetId(),
			[this, sessionCopy]()
			{
				sessionCopy->OnConnected();
				FireEvent(Core::NetworkEvent::Connected, sessionCopy->GetId());
			});

	// 세션의 recv 작업 등록 시작
	if (!QueueRecv(session))
	{
		Utils::Logger::Error("Failed to queue recv - Session " +
							 std::to_string(session->GetId()));

		// Connected가 이미 dispatch됐으므로 세션 제거 전에 Disconnected를 dispatch하여 쌍을 맞춘다.
		auto disconnSession = session;
		mLogicDispatcher.Dispatch(disconnSession->GetId(),
			[this, disconnSession]()
			{
				FireEvent(Core::NetworkEvent::Disconnected, disconnSession->GetId());
			});

		Core::SessionManager::Instance().RemoveSession(session);
		// 세션이 소켓을 소유하므로 pool deleter가 Close()를 호출한다 — close() 직접 호출 금지.
		continue;
	}

		// 연결 정보 로깅
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
	// AsyncIOProvider로부터 I/O 완료 이벤트를 수집하여 처리
	AsyncIO::CompletionEntry entries[64];
	int count = mProvider->ProcessCompletions(entries, 64, 100);

	if (count < 0)
	{
		// ProcessCompletions 자체 에러
		Utils::Logger::Error("ProcessCompletions failed: " +
							 std::string(mProvider->GetLastError()));
		return;
	}

	// 완료 없음 — 프로바이더가 이미 timeoutMs만큼 대기한 후 반환한 것이므로 바로 리턴
	if (count == 0)
	{
		return;
	}

	for (int i = 0; i < count; ++i)
	{
		auto &entry = entries[i];

		// 완료 엔트리의 context(= ConnectionId)로 세션을 조회
		Utils::ConnectionId connId = static_cast<Utils::ConnectionId>(entry.mContext);
		auto session = Core::SessionManager::Instance().GetSession(connId);

		if (!session)
		{
			// 세션이 이미 제거됨 — 완료 이벤트 무시
			continue;
		}

		// I/O 에러 또는 연결 종료 확인
		if (entry.mOsError != 0 || entry.mResult <= 0)
		{
			// ProcessErrorCompletion을 통해 처리:
			//   - Send / Recv 방향별 에러 카운터를 올바르게 증가시킨다.
			//   - session->mAsyncScope 경유로 disconnect를 라우팅하여
			//     다른 경로에서 Close()가 이미 호출된 경우 이중 이벤트를 방지한다.
			ProcessErrorCompletion(session, entry.mType, entry.mOsError);
			continue;
		}

		// I/O 타입에 따라 처리
		switch (entry.mType)
		{
		case AsyncIO::AsyncIOType::Recv:
		{
			// 세션의 recv 버퍼에서 수신 데이터를 가져온다
			const char *recvBuffer = session->GetRecvBuffer();
			ProcessRecvCompletion(session, entry.mResult, recvBuffer);

			// 가드: 세션이 여전히 연결 상태일 때만 recv 재등록.
			// 다른 워커의 송신 에러가 이미 Close()를 호출하여 소켓이 닫혔을 수 있으며,
			// 닫힌 fd에 QueueRecv를 호출하면 재사용된 fd에 epoll/io_uring이 등록될 위험이 있다.
			if (session->IsConnected() && !QueueRecv(session))
			{
				ProcessErrorCompletion(session, AsyncIO::AsyncIOType::Recv, 0);
			}
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
		// 완료 처리 루프
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
// Private 헬퍼 메서드
// =============================================================================

bool LinuxNetworkEngine::CreateListenSocket()
{
	// 논블로킹 TCP 소켓 생성 (SOCK_NONBLOCK 플래그로 한 번에 논블로킹 설정)
	mListenSocket = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

	if (mListenSocket < 0)
	{
		Utils::Logger::Error("Failed to create listen socket: " +
							 std::string(strerror(errno)));
		return false;
	}

	// SO_REUSEADDR 설정 (서버 재시작 시 EADDRINUSE 시간 단축)
	int reuseAddr = 1;
	if (setsockopt(mListenSocket, SOL_SOCKET, SO_REUSEADDR,
				   &reuseAddr, sizeof(reuseAddr)) < 0)
	{
		Utils::Logger::Warn("Failed to set SO_REUSEADDR");
	}

	// 소켓을 포트에 바인드
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

	// listen 시작 (SOMAXCONN 백로그 큐)
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
