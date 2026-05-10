// macOS NetworkEngine 구현

#ifdef __APPLE__

#include "macOSNetworkEngine.h"
#include "../../Utils/Logger.h"
#include "../../Platforms/macOS/KqueueAsyncIOProvider.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

namespace Network::Platforms
{

macOSNetworkEngine::macOSNetworkEngine()
	: mListenSocket(-1), mAcceptBackoffMs(10)
{
	Utils::Logger::Info("macOSNetworkEngine created with kqueue backend");
}

macOSNetworkEngine::~macOSNetworkEngine()
{
	Stop();
}

// =============================================================================
// 플랫폼별 구현 (BaseNetworkEngine 순수 가상 재정의)
// =============================================================================

bool macOSNetworkEngine::InitializePlatform()
{
	// kqueue AsyncIOProvider 생성
	mProvider = std::make_shared<AsyncIO::BSD::KqueueAsyncIOProvider>();
	Utils::Logger::Info("Using kqueue backend");

	// AsyncIOProvider 초기화
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

	// listen 소켓 생성
	if (!CreateListenSocket())
	{
		return false;
	}

	return true;
}

void macOSNetworkEngine::ShutdownPlatform()
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

	Utils::Logger::Info("macOSNetworkEngine platform shutdown complete");
}

bool macOSNetworkEngine::StartPlatformIO()
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

void macOSNetworkEngine::StopPlatformIO()
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

void macOSNetworkEngine::AcceptLoop()
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

		// 클라이언트 소켓을 논블로킹 모드로 설정 (kqueue 등록 전에 수행)
		int flags = fcntl(clientSocket, F_GETFL, 0);
		if (flags != -1)
		{
			fcntl(clientSocket, F_SETFL, flags | O_NONBLOCK);
		}

		// macOS 전용: 소켓에 SO_NOSIGPIPE 설정.
		// 원격이 닫힌 소켓에 쓰면 기본적으로 SIGPIPE가 발생하여 프로세스가 종료된다.
		// SO_NOSIGPIPE는 시그널 대신 send()가 EPIPE를 반환하도록 한다.
		// (MSG_NOSIGNAL은 Linux 전용이므로 macOS에서 사용할 수 없다.)
		{
			int nosigpipe = 1;
			if (setsockopt(clientSocket, SOL_SOCKET, SO_NOSIGPIPE,
						   &nosigpipe, sizeof(nosigpipe)) < 0)
			{
				Utils::Logger::Warn("Failed to set SO_NOSIGPIPE on socket " +
									std::to_string(clientSocket) + ": " +
									std::string(strerror(errno)));
			}
		}

		// 세션 생성
		Core::SessionRef session =
			Core::SessionManager::Instance().CreateSession(clientSocket);
		if (!session)
		{
			close(clientSocket);
			continue;
		}

		// 클라이언트 소켓을 kqueue 비동기 I/O 프로바이더에 등록
		auto assocResult = mProvider->AssociateSocket(
			clientSocket,
			static_cast<AsyncIO::RequestContext>(session->GetId()));
		if (assocResult != AsyncIO::AsyncIOError::Success)
		{
			Utils::Logger::Error(
				"Failed to associate socket with kqueue - Session " +
				std::to_string(session->GetId()) + ": " +
				std::string(mProvider->GetLastError()));
			Core::SessionManager::Instance().RemoveSession(session);
			// close(clientSocket) 호출 금지 — RemoveSession → session->Close()가
			// pool deleter를 통해 소켓을 이미 닫는다.
			// 두 번째 close()는 fd의 use-after-free다.
			continue;
		}

		// 세션에 async 프로바이더를 연결하여 EVFILT_WRITE 경유 송신 큐잉을 활성화
		session->SetAsyncProvider(mProvider);

		// 연결 수 통계 업데이트 (memory_order_relaxed)
		mTotalConnections.fetch_add(1, std::memory_order_relaxed);

		// KeyedDispatcher를 통해 Connected 이벤트를 로직 스레드에 비동기 디스패치.
		// sessionId를 키로 사용하면 Connected 및 이후 모든 이벤트가 동일 워커로 라우팅된다
		// (LinuxNetworkEngine과 동일한 패턴).
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
			// close(clientSocket) 호출 금지 — RemoveSession → session->Close()가 이미 fd를 닫는다.
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

void macOSNetworkEngine::ProcessCompletions()
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
			// 닫힌 fd에 QueueRecv를 호출하면 재사용된 fd에 kqueue가 등록될 위험이 있다.
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

void macOSNetworkEngine::WorkerThread()
{
	Utils::Logger::Debug("Worker thread started");

	while (mRunning)
	{
		// 완료 처리 루프
		ProcessCompletions();
	}

	Utils::Logger::Debug("Worker thread stopped");
}

bool macOSNetworkEngine::QueueRecv(const Core::SessionRef &session)
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

bool macOSNetworkEngine::CreateListenSocket()
{
	// TCP 소켓 생성 (fcntl로 별도 논블로킹 설정 필요)
	mListenSocket = socket(AF_INET, SOCK_STREAM, 0);

	if (mListenSocket < 0)
	{
		Utils::Logger::Error("Failed to create listen socket: " +
							 std::string(strerror(errno)));
		return false;
	}

	// listen 소켓을 논블로킹 모드로 설정
	int flags = fcntl(mListenSocket, F_GETFL, 0);
	if (flags != -1)
	{
		fcntl(mListenSocket, F_SETFL, flags | O_NONBLOCK);
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

#endif // __APPLE__
