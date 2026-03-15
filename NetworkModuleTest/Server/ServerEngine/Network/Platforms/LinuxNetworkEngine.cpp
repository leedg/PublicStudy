// Linux NetworkEngine implementation

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
// Platform-specific implementation
// =============================================================================

bool LinuxNetworkEngine::InitializePlatform()
{
	// Create AsyncIOProvider based on mode
	if (mMode == Mode::Epoll)
	{
		mProvider = std::make_shared<AsyncIO::Linux::EpollAsyncIOProvider>();
		Utils::Logger::Info("Using epoll backend");
	}
	else // IOUring
	{
		mProvider = std::make_shared<AsyncIO::Linux::IOUringAsyncIOProvider>();
		Utils::Logger::Info("Using io_uring backend");
	}

	// Initialize provider
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

	// Create listen socket
	if (!CreateListenSocket())
	{
		return false;
	}

	return true;
}

void LinuxNetworkEngine::ShutdownPlatform()
{
	// Close listen socket
	if (mListenSocket != -1)
	{
		close(mListenSocket);
		mListenSocket = -1;
	}

	// Shutdown provider
	if (mProvider)
	{
		mProvider->Shutdown();
	}

	Utils::Logger::Info("LinuxNetworkEngine platform shutdown complete");
}

bool LinuxNetworkEngine::StartPlatformIO()
{
	// Start worker threads for completion processing
	uint32_t workerCount = std::thread::hardware_concurrency();
	if (workerCount == 0)
	{
		workerCount = 4;
	}

	for (uint32_t i = 0; i < workerCount; ++i)
	{
		mWorkerThreads.emplace_back([this]() { this->WorkerThread(); });
	}

	// Start accept thread
	mAcceptThread = std::thread([this]() { this->AcceptLoop(); });

	Utils::Logger::Info("Started " + std::to_string(workerCount) +
						" worker threads + 1 accept thread");
	return true;
}

void LinuxNetworkEngine::StopPlatformIO()
{
	// Stop accept thread
	if (mListenSocket != -1)
	{
		close(mListenSocket);
		mListenSocket = -1;
	}

	if (mAcceptThread.joinable())
	{
		mAcceptThread.join();
	}

	// Stop worker threads (they check mRunning flag)
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
		// Accept incoming connection
		int clientSocket = accept(
			mListenSocket,
			reinterpret_cast<sockaddr *>(&clientAddr),
			&clientAddrSize);

		if (clientSocket < 0)
		{
			if (errno == EINTR || errno == EBADF)
			{
				// Socket closed (shutdown signal)
				break;
			}

			if (errno == EAGAIN || errno == EWOULDBLOCK)
			{
				// Non-blocking listen socket — no connection pending; yield briefly.
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				continue;
			}

			if (errno == EMFILE || errno == ENFILE || errno == ENOMEM)
			{
				// System resource exhaustion — back off longer before retrying.
				Utils::Logger::Error("Accept resource exhaustion (" +
									 std::string(strerror(errno)) +
									 ") - sleeping 5 s");
				std::this_thread::sleep_for(std::chrono::seconds(5));
				continue;
			}

			Utils::Logger::Error("Accept failed: " + std::string(strerror(errno)));

			// Exponential backoff on error (member var, not static)
			std::this_thread::sleep_for(std::chrono::milliseconds(mAcceptBackoffMs));
			mAcceptBackoffMs = (std::min)(mAcceptBackoffMs * 2, 1000);
			continue;
		}

		// Reset backoff on success
		mAcceptBackoffMs = 10;

		// Set socket to non-blocking mode
		int flags = fcntl(clientSocket, F_GETFL, 0);
		if (flags != -1)
		{
			fcntl(clientSocket, F_SETFL, flags | O_NONBLOCK);
		}

		// Create session
		Core::SessionRef session =
			Core::SessionManager::Instance().CreateSession(clientSocket);
		if (!session)
		{
			close(clientSocket);
			continue;
		}

		// Associate client socket with async I/O provider (epoll/io_uring)
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
			// Session::Close() called by pool deleter on last ref drop —
			//          do NOT close(clientSocket) here (double-close / fd-recycle risk).
			continue;
		}

		// Set async provider so session can queue sends via EPOLLOUT
		session->SetAsyncProvider(mProvider);

		// Update stats (atomic)
		mTotalConnections.fetch_add(1, std::memory_order_relaxed);

		// Fire Connected event asynchronously on logic thread
		auto sessionCopy = session;
		mLogicDispatcher.Dispatch(sessionCopy->GetId(),
			[this, sessionCopy]()
			{
				sessionCopy->OnConnected();
				FireEvent(Core::NetworkEvent::Connected, sessionCopy->GetId());
			});

	// Start receiving on this session
	if (!QueueRecv(session))
	{
		Utils::Logger::Error("Failed to queue recv - Session " +
							 std::to_string(session->GetId()));
		Core::SessionManager::Instance().RemoveSession(session);
			// Session owns the socket; pool deleter calls Close() — do NOT
			//          close(clientSocket) here.
			continue;
	}

		// Log connection
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
	// Process completions from AsyncIOProvider
	AsyncIO::CompletionEntry entries[64];
	int count = mProvider->ProcessCompletions(entries, 64, 100);

	if (count < 0)
	{
		// Error occurred
		Utils::Logger::Error("ProcessCompletions failed: " +
							 std::string(mProvider->GetLastError()));
		return;
	}

	// No completions - provider already waited with timeout, just return
	if (count == 0)
	{
		return;
	}

	for (int i = 0; i < count; ++i)
	{
		auto &entry = entries[i];

		// Get session from context (ConnectionId stored in context)
		Utils::ConnectionId connId = static_cast<Utils::ConnectionId>(entry.mContext);
		auto session = Core::SessionManager::Instance().GetSession(connId);

		if (!session)
		{
			// Session no longer exists
			continue;
		}

		// Check for errors
		if (entry.mOsError != 0 || entry.mResult <= 0)
		{
			// Dispatch through ProcessErrorCompletion — increments the correct
			//          per-direction error counter (Send vs Recv) and routes disconnect
			//          via session->mAsyncScope, preventing double-event if Close() was
			//          already called from a concurrent path.
			//          Previous code used mLogicDispatcher.Dispatch() directly, bypassing
			//          AsyncScope and losing type distinction for stats.
			ProcessErrorCompletion(session, entry.mType, entry.mOsError);
			continue;
		}

		// Process based on I/O type
		switch (entry.mType)
		{
		case AsyncIO::AsyncIOType::Recv:
		{
			// Get received data from session's recv buffer
			const char *recvBuffer = session->GetRecvBuffer();
			ProcessRecvCompletion(session, entry.mResult, recvBuffer);

			// Guard: only re-queue recv if the session is still connected.
			//          A concurrent Send-error on another worker may have already
			//          called Close() on this session's socket between ProcessRecvCompletion
			//          and here. Calling QueueRecv on a closed fd risks registering
			//          epoll/io_uring interest on a recycled file descriptor.
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
		// Process completions in loop
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
// Private helper methods
// =============================================================================

bool LinuxNetworkEngine::CreateListenSocket()
{
	// Create socket
	mListenSocket = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

	if (mListenSocket < 0)
	{
		Utils::Logger::Error("Failed to create listen socket: " +
							 std::string(strerror(errno)));
		return false;
	}

	// Set socket options
	int reuseAddr = 1;
	if (setsockopt(mListenSocket, SOL_SOCKET, SO_REUSEADDR,
				   &reuseAddr, sizeof(reuseAddr)) < 0)
	{
		Utils::Logger::Warn("Failed to set SO_REUSEADDR");
	}

	// Bind socket
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

	// Listen for connections
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
