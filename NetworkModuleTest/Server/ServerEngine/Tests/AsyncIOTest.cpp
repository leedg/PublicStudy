// AsyncIO Provider Test Suite - Simple verification (no GTest
// dependency) ???: AsyncIO ??????????- ?????????(GTest ????????)

#include "Network/Core/AsyncIOProvider.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>

using namespace Network::AsyncIO;

// =============================================================================
// Test Functions
// ???: ????????
// =============================================================================

void TestPlatformDetection()
{
	// Test platform detection
	// ???: ???????? ?????
	std::cout << "=== Platform Detection Test ===" << std::endl;

	PlatformType platform = GetCurrentPlatform();

#ifdef _WIN32
	std::cout << "Current Platform: Windows (IOCP/RIO)" << std::endl;
	if (platform == PlatformType::IOCP || platform == PlatformType::RIO)
	{
		std::cout << "[PASS] Platform detected correctly" << std::endl;
	}
	else
	{
		std::cout << "[FAIL] Unexpected platform type" << std::endl;
	}
#elif __linux__
	std::cout << "Current Platform: Linux (epoll/io_uring)" << std::endl;
	if (platform == PlatformType::Epoll || platform == PlatformType::IOUring)
	{
		std::cout << "[PASS] Platform detected correctly" << std::endl;
	}
	else
	{
		std::cout << "[FAIL] Unexpected platform type" << std::endl;
	}
#elif __APPLE__
	std::cout << "Current Platform: macOS (kqueue)" << std::endl;
	if (platform == PlatformType::Kqueue)
	{
		std::cout << "[PASS] Platform detected correctly" << std::endl;
	}
	else
	{
		std::cout << "[FAIL] Unexpected platform type" << std::endl;
	}
#else
	std::cout << "[FAIL] Unknown platform" << std::endl;
#endif
}

void TestPlatformSupport()
{
	// Test IsPlatformSupported and GetSupportedPlatforms
	// ???: IsPlatformSupported ??GetSupportedPlatforms ?????
	std::cout << "\n=== Platform Support Test ===" << std::endl;

	// Get supported platforms list
	// ???: ???????????? ???
	size_t count = 0;
	const char **platforms = GetSupportedPlatforms(count);

	std::cout << "Supported platforms (" << count << "):" << std::endl;
	for (size_t i = 0; i < count; ++i)
	{
		bool supported = IsPlatformSupported(platforms[i]);
		std::cout << "  " << platforms[i] << ": "
				  << (supported ? "available" : "not available") << std::endl;
	}

	std::cout << "[PASS] Platform support query completed" << std::endl;
}

void TestAsyncIOProviderCreation()
{
	// Test automatic provider creation
	// ???: ??? ???????? ?????
	std::cout << "\n=== AsyncIOProvider Creation Test ===" << std::endl;

	// Create with automatic platform selection
	// ???: ??? ??????????? ???
	auto provider = CreateAsyncIOProvider();

	if (provider)
	{
		std::cout << "[PASS] Provider created successfully" << std::endl;

		// Initialize with doc-specified interface
		// ???: ??? ??? ????????? ?????
		AsyncIOError err = provider->Initialize(256, 1000);
		if (err == AsyncIOError::Success)
		{
			std::cout << "[PASS] Provider initialized successfully"
					  << std::endl;

			// Check IsInitialized
			// ???: IsInitialized ???
			if (provider->IsInitialized())
			{
				std::cout << "[PASS] IsInitialized returns true" << std::endl;
			}

			// Check GetInfo
			// ???: GetInfo ???
			const ProviderInfo &info = provider->GetInfo();
			std::cout << "Backend: " << info.mName << std::endl;
			std::cout << "Buffer Registration: "
					  << (info.mSupportsBufferReg ? "yes" : "no") << std::endl;
			std::cout << "Batching: " << (info.mSupportsBatching ? "yes" : "no")
					  << std::endl;

			// Check GetStats
			// ???: GetStats ???
			ProviderStats stats = provider->GetStats();
			std::cout << "Total Requests: " << stats.mTotalRequests
					  << std::endl;

			// Check GetLastError
			// ???: GetLastError ???
			const char *lastErr = provider->GetLastError();
			std::cout << "Last Error: \"" << (lastErr ? lastErr : "") << "\""
					  << std::endl;

			// Test FlushRequests (should be no-op or success)
			// ???: FlushRequests ?????(no-op ??? ??????????
			AsyncIOError flushErr = provider->FlushRequests();
			if (flushErr == AsyncIOError::Success)
			{
				std::cout << "[PASS] FlushRequests succeeded" << std::endl;
			}

			provider->Shutdown();
			std::cout << "[PASS] Provider shutdown successfully" << std::endl;

			// Verify IsInitialized after shutdown
			// ???: ??? ??IsInitialized ???
			if (!provider->IsInitialized())
			{
				std::cout << "[PASS] IsInitialized returns false after shutdown"
						  << std::endl;
			}
		}
		else
		{
			std::cout << "[FAIL] Provider initialization failed" << std::endl;
		}
	}
	else
	{
		std::cout << "[FAIL] Failed to create provider" << std::endl;
	}
}

void TestNamedProviderCreation()
{
	// Test named provider creation (CreateAsyncIOProvider with
	// platformHint) ???: ??? ??? ???????? ?????
	std::cout << "\n=== Named Provider Creation Test ===" << std::endl;

#ifdef _WIN32
	auto iocpProvider = CreateAsyncIOProvider("IOCP");
	if (iocpProvider)
	{
		std::cout << "[PASS] IOCP provider created by name" << std::endl;
	}
	else
	{
		std::cout << "[FAIL] IOCP provider creation by name failed"
				  << std::endl;
	}
#elif __linux__
	auto epollProvider = CreateAsyncIOProvider("epoll");
	if (epollProvider)
	{
		std::cout << "[PASS] epoll provider created by name" << std::endl;
	}
	else
	{
		std::cout << "[FAIL] epoll provider creation by name failed"
				  << std::endl;
	}
#elif __APPLE__
	auto kqueueProvider = CreateAsyncIOProvider("kqueue");
	if (kqueueProvider)
	{
		std::cout << "[PASS] kqueue provider created by name" << std::endl;
	}
	else
	{
		std::cout << "[FAIL] kqueue provider creation by name failed"
				  << std::endl;
	}
#endif

	// Test unsupported platform name
	// ???: ??????? ??? ???????? ?????
	auto nullProvider = CreateAsyncIOProvider("nonexistent");
	if (!nullProvider)
	{
		std::cout << "[PASS] Unsupported platform returns nullptr" << std::endl;
	}
}

#ifdef _WIN32
void TestRIOEndToEnd()
{
	std::cout << "\n=== RIO Loopback Test ===" << std::endl;

	auto provider = CreateAsyncIOProvider("RIO");
	if (!provider)
	{
		std::cout << "[FAIL] RIO provider not available" << std::endl;
		return;
	}

	if (provider->Initialize(256, 512) != AsyncIOError::Success)
	{
		std::cout << "[FAIL] RIO provider init failed: "
				  << provider->GetLastError() << std::endl;
		return;
	}

	SOCKET listenSock =
		WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0,
				  WSA_FLAG_REGISTERED_IO | WSA_FLAG_OVERLAPPED);
	if (listenSock == INVALID_SOCKET)
	{
		std::cout << "[FAIL] WSASocket(listen) failed" << std::endl;
		provider->Shutdown();
		return;
	}

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = 0;

	if (bind(listenSock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) ==
		SOCKET_ERROR)
	{
		std::cout << "[FAIL] bind failed" << std::endl;
		closesocket(listenSock);
		provider->Shutdown();
		return;
	}

	if (listen(listenSock, 1) == SOCKET_ERROR)
	{
		std::cout << "[FAIL] listen failed" << std::endl;
		closesocket(listenSock);
		provider->Shutdown();
		return;
	}

	int addrLen = sizeof(addr);
	if (getsockname(listenSock, reinterpret_cast<sockaddr *>(&addr),
					&addrLen) == SOCKET_ERROR)
	{
		std::cout << "[FAIL] getsockname failed" << std::endl;
		closesocket(listenSock);
		provider->Shutdown();
		return;
	}

	SOCKET clientSock =
		WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0,
				  WSA_FLAG_REGISTERED_IO | WSA_FLAG_OVERLAPPED);
	if (clientSock == INVALID_SOCKET)
	{
		std::cout << "[FAIL] WSASocket(client) failed" << std::endl;
		closesocket(listenSock);
		provider->Shutdown();
		return;
	}

	if (connect(clientSock, reinterpret_cast<sockaddr *>(&addr),
				sizeof(addr)) == SOCKET_ERROR)
	{
		std::cout << "[FAIL] connect failed" << std::endl;
		closesocket(clientSock);
		closesocket(listenSock);
		provider->Shutdown();
		return;
	}

	SOCKET serverSock = accept(listenSock, nullptr, nullptr);
	closesocket(listenSock);
	if (serverSock == INVALID_SOCKET)
	{
		std::cout << "[FAIL] accept failed" << std::endl;
		closesocket(clientSock);
		provider->Shutdown();
		return;
	}

	const char payload[] = "RIO-PING";
	char recvBuf[64] = {};

	if (provider->RecvAsync(serverSock, recvBuf, sizeof(payload), 1) !=
		AsyncIOError::Success)
	{
		std::cout << "[FAIL] RIO RecvAsync failed: "
				  << provider->GetLastError() << std::endl;
		closesocket(serverSock);
		closesocket(clientSock);
		provider->Shutdown();
		return;
	}

	if (provider->SendAsync(clientSock, payload, sizeof(payload), 2) !=
		AsyncIOError::Success)
	{
		std::cout << "[FAIL] RIO SendAsync failed: "
				  << provider->GetLastError() << std::endl;
		closesocket(serverSock);
		closesocket(clientSock);
		provider->Shutdown();
		return;
	}

	provider->FlushRequests();

	bool gotRecv = false;
	bool gotSend = false;
	CompletionEntry entries[4] = {};

	for (int attempt = 0; attempt < 20 && (!gotRecv || !gotSend); ++attempt)
	{
		int count = provider->ProcessCompletions(entries, 4, 100);
		if (count < 0)
		{
			std::cout << "[FAIL] RIO completion error: "
					  << provider->GetLastError() << std::endl;
			break;
		}
		for (int i = 0; i < count; ++i)
		{
			if (entries[i].mType == AsyncIOType::Recv)
			{
				gotRecv = (entries[i].mResult > 0);
			}
			else if (entries[i].mType == AsyncIOType::Send)
			{
				gotSend = (entries[i].mResult > 0);
			}
		}
		if (!gotRecv || !gotSend)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
	}

	if (gotRecv && gotSend)
	{
		if (std::memcmp(recvBuf, payload, sizeof(payload)) == 0)
		{
			std::cout << "[PASS] RIO send/recv completed" << std::endl;
		}
		else
		{
			std::cout << "[FAIL] RIO data mismatch" << std::endl;
		}
	}
	else
	{
		std::cout << "[FAIL] RIO send/recv did not complete" << std::endl;
	}

	closesocket(serverSock);
	closesocket(clientSock);
	provider->Shutdown();
}
#endif

// =============================================================================
// Main Entry Point
// ???: ??? ?????
// =============================================================================

int main(int argc, char *argv[])
{
	std::cout << "====================================" << std::endl;
	std::cout << "AsyncIO Provider Test Suite" << std::endl;
	std::cout << "====================================" << std::endl;

	try
	{
		TestPlatformDetection();
		TestPlatformSupport();
		TestAsyncIOProviderCreation();
		TestNamedProviderCreation();
#ifdef _WIN32
		TestRIOEndToEnd();
#endif

		std::cout << "\n====================================" << std::endl;
		std::cout << "All tests completed" << std::endl;
		std::cout << "====================================" << std::endl;
	}
	catch (const std::exception &e)
	{
		std::cerr << "Test exception: " << e.what() << std::endl;
		return 1;
	}

	return 0;
}
