// English: Windows NetworkEngine implementation

#ifdef _WIN32

#include "WindowsNetworkEngine.h"
#include "../../Platforms/Windows/IocpAsyncIOProvider.h"
#include "../../Platforms/Windows/RIOAsyncIOProvider.h"
#include "../../Utils/Logger.h"
#include "../Core/SendBufferPool.h"
#include <algorithm>
#include <chrono>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

namespace Network::Platforms
{

WindowsNetworkEngine::WindowsNetworkEngine(Mode mode)
	: mMode(mode), mListenSocket(INVALID_SOCKET), mAcceptBackoffMs(10)
{
	Utils::Logger::Info("WindowsNetworkEngine created with mode: " +
						std::string(mode == Mode::IOCP ? "IOCP" : "RIO"));
}

WindowsNetworkEngine::~WindowsNetworkEngine()
{
	Stop();
}

bool WindowsNetworkEngine::InitializePlatform()
{
	if (!InitializeWinsock())
	{
		return false;
	}

	if (mMode == Mode::IOCP)
	{
		mProvider = std::make_shared<AsyncIO::Windows::IocpAsyncIOProvider>();
		Utils::Logger::Info("Using IOCP backend");
	}
	else
	{
		mProvider = std::make_shared<AsyncIO::Windows::RIOAsyncIOProvider>();
		Utils::Logger::Info("Using RIO backend");
	}

	const size_t effectiveMax =
		mMaxConnections > 0 ? static_cast<size_t>(mMaxConnections) : 128;
	auto error = mProvider->Initialize(effectiveMax * 2 + 64, effectiveMax);
	if (error != AsyncIO::AsyncIOError::Success)
	{
		Utils::Logger::Error("Failed to initialize AsyncIOProvider: " +
							 std::string(mProvider->GetLastError()));
		return false;
	}

	// English: Initialize IOCP send buffer pool (4 concurrent sends per connection).
	//          RIO path uses its own slab pool; only initialize for IOCP mode.
	// 한글: IOCP 전송 버퍼 풀 초기화 (연결당 동시 전송 4개 기준).
	//       RIO 경로는 자체 slab 풀 사용; IOCP 모드에서만 초기화.
	if (mMode == Mode::IOCP)
	{
		Core::SendBufferPool::Instance().Initialize(
			effectiveMax * 4, Core::SEND_BUFFER_SIZE);
		Utils::Logger::Info("SendBufferPool initialized: " +
							std::to_string(effectiveMax * 4) + " slots × " +
							std::to_string(Core::SEND_BUFFER_SIZE) + " bytes");
	}

	if (!CreateListenSocket())
	{
		return false;
	}

	return true;
}

void WindowsNetworkEngine::ShutdownPlatform()
{
	if (mListenSocket != INVALID_SOCKET)
	{
		closesocket(mListenSocket);
		mListenSocket = INVALID_SOCKET;
	}

	if (mProvider)
	{
		mProvider->Shutdown();
	}

	WSACleanup();
	Utils::Logger::Info("WindowsNetworkEngine platform shutdown complete");
}

bool WindowsNetworkEngine::StartPlatformIO()
{
	uint32_t workerCount = std::thread::hardware_concurrency();
	if (workerCount == 0)
	{
		workerCount = 4;
	}

	for (uint32_t i = 0; i < workerCount; ++i)
	{
		mWorkerThreads.emplace_back([this]() { this->WorkerThread(); });
	}

	mAcceptThread = std::thread([this]() { this->AcceptLoop(); });

	Utils::Logger::Info("Started " + std::to_string(workerCount) +
						" worker threads + 1 accept thread");
	return true;
}

void WindowsNetworkEngine::StopPlatformIO()
{
	if (mListenSocket != INVALID_SOCKET)
	{
		closesocket(mListenSocket);
		mListenSocket = INVALID_SOCKET;
	}

	if (mAcceptThread.joinable())
	{
		mAcceptThread.join();
	}

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
		SOCKET clientSocket = accept(
			mListenSocket, reinterpret_cast<sockaddr *>(&clientAddr),
			&clientAddrSize);

		if (clientSocket == INVALID_SOCKET)
		{
			int error = WSAGetLastError();
			if (error == WSAEINTR || error == WSAENOTSOCK)
			{
				break;
			}

			Utils::Logger::Error("Accept failed: " + std::to_string(error));
			std::this_thread::sleep_for(std::chrono::milliseconds(mAcceptBackoffMs));
			mAcceptBackoffMs = (std::min)(mAcceptBackoffMs * 2, 1000);
			continue;
		}

		// English: Reset backoff on success
		// 한글: 성공 시 백오프 리셋
		mAcceptBackoffMs = 10;

		Core::SessionRef session =
			Core::SessionManager::Instance().CreateSession(clientSocket);
		if (!session)
		{
			closesocket(clientSocket);
			continue;
		}

		auto assocResult = mProvider->AssociateSocket(
			clientSocket,
			static_cast<AsyncIO::RequestContext>(session->GetId()));
		if (assocResult != AsyncIO::AsyncIOError::Success)
		{
			Utils::Logger::Error(
				"Failed to associate socket with async backend - Session " +
				std::to_string(session->GetId()) + ": " +
				std::string(mProvider->GetLastError()));
			Core::SessionManager::Instance().RemoveSession(session);
			continue;
		}

		if (mMode == Mode::RIO)
		{
			session->SetAsyncProvider(mProvider);
		}

		mTotalConnections.fetch_add(1, std::memory_order_relaxed);

		auto sessionCopy = session;
		mLogicThreadPool.Submit(
			[this, sessionCopy]()
			{
				sessionCopy->OnConnected();
				FireEvent(Core::NetworkEvent::Connected, sessionCopy->GetId());
			});

		bool recvQueued = false;
		if (mMode == Mode::RIO)
		{
			auto recvResult = mProvider->RecvAsync(
				clientSocket, session->GetRecvBuffer(),
				session->GetRecvBufferSize(),
				static_cast<AsyncIO::RequestContext>(session->GetId()));
			recvQueued = (recvResult == AsyncIO::AsyncIOError::Success);
			if (recvQueued)
			{
				mProvider->FlushRequests();
			}
		}
		else
		{
			recvQueued = session->PostRecv();
		}

		if (!recvQueued)
		{
			Utils::Logger::Error("Failed to queue recv - Session " +
				std::to_string(session->GetId()) + ": " +
				std::string(mProvider->GetLastError()));
			Core::SessionManager::Instance().RemoveSession(session);
			continue;
		}

		char clientIP[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, sizeof(clientIP));
		Utils::Logger::Info("Client connected - IP: " + std::string(clientIP) +
							":" + std::to_string(ntohs(clientAddr.sin_port)) +
							" (Session " +
							std::to_string(session->GetId()) + ")");
	}

	Utils::Logger::Info("Accept thread stopped");
}

void WindowsNetworkEngine::ProcessCompletions()
{
	AsyncIO::CompletionEntry entries[64];
	int count = mProvider->ProcessCompletions(entries, 64, 100);

	if (count < 0)
	{
		Utils::Logger::Error("ProcessCompletions failed: " +
						 std::string(mProvider->GetLastError()));
		return;
	}

	if (count == 0)
	{
		return;
	}

	for (int i = 0; i < count; ++i)
	{
		auto &entry = entries[i];
		Utils::ConnectionId connId = static_cast<Utils::ConnectionId>(entry.mContext);
		auto session = Core::SessionManager::Instance().GetSession(connId);
		if (!session)
		{
			continue;
		}

		if (entry.mOsError != 0 || entry.mResult <= 0)
		{
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

		switch (entry.mType)
		{
		case AsyncIO::AsyncIOType::Recv:
		{
			const char *recvBuffer = (mMode == Mode::RIO)
				? session->GetRecvBuffer()
				: session->GetRecvContext().buffer;
			ProcessRecvCompletion(session, entry.mResult, recvBuffer);

			bool recvQueued = false;
			if (mMode == Mode::RIO)
			{
				auto recvResult = mProvider->RecvAsync(
					session->GetSocket(), session->GetRecvBuffer(),
					session->GetRecvBufferSize(),
					static_cast<AsyncIO::RequestContext>(session->GetId()));
				recvQueued = (recvResult == AsyncIO::AsyncIOError::Success);
				if (recvQueued)
				{
					mProvider->FlushRequests();
				}
			}
			else
			{
				recvQueued = session->PostRecv();
			}

			if (!recvQueued)
			{
				Utils::Logger::Error(
					"Failed to queue next recv - Session " +
					std::to_string(session->GetId()) + ": " +
					std::string(mProvider->GetLastError()));

				auto sessionCopy = session;
				mLogicThreadPool.Submit(
					[this, sessionCopy]()
					{
						sessionCopy->OnDisconnected();
						FireEvent(Core::NetworkEvent::Disconnected,
								  sessionCopy->GetId());
					});
				Core::SessionManager::Instance().RemoveSession(session);
			}
			break;
		}

		case AsyncIO::AsyncIOType::Send:
		{
			if (mMode == Mode::RIO)
			{
				FireEvent(Core::NetworkEvent::DataSent, session->GetId());
			}
			else
			{
				ProcessSendCompletion(session, entry.mResult);
			}
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
		ProcessCompletions();
	}
	Utils::Logger::Debug("Worker thread stopped");
}

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
	DWORD socketFlags = WSA_FLAG_OVERLAPPED;
	if (mMode == Mode::RIO)
	{
		socketFlags |= WSA_FLAG_REGISTERED_IO;
	}

	mListenSocket =
		WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, socketFlags);
	if (mListenSocket == INVALID_SOCKET)
	{
		Utils::Logger::Error("Failed to create listen socket: " +
						 std::to_string(WSAGetLastError()));
		return false;
	}

	BOOL reuseAddr = TRUE;
	if (setsockopt(mListenSocket, SOL_SOCKET, SO_REUSEADDR,
				   reinterpret_cast<const char *>(&reuseAddr),
				   sizeof(reuseAddr)) == SOCKET_ERROR)
	{
		Utils::Logger::Warn("Failed to set SO_REUSEADDR");
	}

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
