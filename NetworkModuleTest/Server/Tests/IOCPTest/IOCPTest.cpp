// IOCP AsyncIOProvider test suite.
//          No GTest dependency - uses std::cout (same pattern as RIOTest.cpp).

#ifdef _WIN32
#include "Network/Core/AsyncIOProvider.h"
#include "Platforms/Windows/IocpAsyncIOProvider.h"
#include <chrono>
#include <iostream>
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>

using namespace Network::AsyncIO;
using namespace Network::AsyncIO::Windows;

static int gPassed = 0, gFailed = 0;

class WinsockGuard
{
  public:
	WinsockGuard()
	{
		mReady = (WSAStartup(MAKEWORD(2, 2), &mWsaData) == 0);
	}

	~WinsockGuard()
	{
		if (mReady)
		{
			WSACleanup();
		}
	}

	bool IsReady() const
	{
		return mReady;
	}

  private:
	WSADATA mWsaData{};
	bool mReady = false;
};

static void Pass(const char *name)
{
	std::cout << "[PASS] " << name << "\n";
	++gPassed;
}

static void Fail(const char *name, const char *reason)
{
	std::cout << "[FAIL] " << name << " - " << reason << "\n";
	++gFailed;
}

void TestIOCPProviderInit()
{
	const char *name = "IOCPProviderInit";
	IocpAsyncIOProvider provider;
	auto err = provider.Initialize(256, 128);
	if (err == AsyncIOError::Success)
	{
		Pass(name);
	}
	else
	{
		Fail(name, provider.GetLastError());
	}

	if (provider.IsInitialized())
	{
		provider.Shutdown();
	}
}

void TestIOCPLoopbackCompletions()
{
	const char *name = "IOCPLoopbackCompletions";
	IocpAsyncIOProvider provider;
	if (provider.Initialize(256, 128) != AsyncIOError::Success)
	{
		Fail(name, provider.GetLastError());
		return;
	}

	SOCKET listenSock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0,
								  WSA_FLAG_OVERLAPPED);
	if (listenSock == INVALID_SOCKET)
	{
		Fail(name, "WSASocket(listen) failed");
		provider.Shutdown();
		return;
	}

	auto cleanup = [&]()
	{
		if (listenSock != INVALID_SOCKET)
		{
			closesocket(listenSock);
			listenSock = INVALID_SOCKET;
		}
	};

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = 0;

	if (bind(listenSock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) ==
		SOCKET_ERROR)
	{
		Fail(name, "bind failed");
		cleanup();
		provider.Shutdown();
		return;
	}

	if (listen(listenSock, 1) == SOCKET_ERROR)
	{
		Fail(name, "listen failed");
		cleanup();
		provider.Shutdown();
		return;
	}

	int addrLen = sizeof(addr);
	if (getsockname(listenSock, reinterpret_cast<sockaddr *>(&addr), &addrLen) ==
		SOCKET_ERROR)
	{
		Fail(name, "getsockname failed");
		cleanup();
		provider.Shutdown();
		return;
	}

	SOCKET clientSock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0,
								  WSA_FLAG_OVERLAPPED);
	if (clientSock == INVALID_SOCKET)
	{
		Fail(name, "WSASocket(client) failed");
		cleanup();
		provider.Shutdown();
		return;
	}

	if (connect(clientSock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) ==
		SOCKET_ERROR)
	{
		Fail(name, "connect failed");
		closesocket(clientSock);
		cleanup();
		provider.Shutdown();
		return;
	}

	SOCKET serverSock = accept(listenSock, nullptr, nullptr);
	closesocket(listenSock);
	listenSock = INVALID_SOCKET;
	if (serverSock == INVALID_SOCKET)
	{
		Fail(name, "accept failed");
		closesocket(clientSock);
		provider.Shutdown();
		return;
	}

	if (provider.AssociateSocket(serverSock, 1) != AsyncIOError::Success)
	{
		Fail(name, provider.GetLastError());
		closesocket(serverSock);
		closesocket(clientSock);
		provider.Shutdown();
		return;
	}

	if (provider.AssociateSocket(clientSock, 2) != AsyncIOError::Success)
	{
		Fail(name, provider.GetLastError());
		closesocket(serverSock);
		closesocket(clientSock);
		provider.Shutdown();
		return;
	}

	const char payload[] = "IOCP-PING";
	char recvBuf[64] = {};

	if (provider.RecvAsync(serverSock, recvBuf, sizeof(payload), 1001) !=
		AsyncIOError::Success)
	{
		Fail(name, provider.GetLastError());
		closesocket(serverSock);
		closesocket(clientSock);
		provider.Shutdown();
		return;
	}

	if (provider.SendAsync(clientSock, payload, sizeof(payload), 1002) !=
		AsyncIOError::Success)
	{
		Fail(name, provider.GetLastError());
		closesocket(serverSock);
		closesocket(clientSock);
		provider.Shutdown();
		return;
	}

	provider.FlushRequests();

	bool gotRecv = false;
	bool gotSend = false;
	CompletionEntry entries[8] = {};

	for (int attempt = 0; attempt < 30 && (!gotRecv || !gotSend); ++attempt)
	{
		int count = provider.ProcessCompletions(entries, 8, 100);
		if (count < 0)
		{
			Fail(name, provider.GetLastError());
			break;
		}

		for (int i = 0; i < count; ++i)
		{
			if (entries[i].mType == AsyncIOType::Recv && entries[i].mResult > 0)
			{
				gotRecv = true;
			}
			else if (entries[i].mType == AsyncIOType::Send &&
					 entries[i].mResult > 0)
			{
				gotSend = true;
			}
		}

		if (!gotRecv || !gotSend)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
	}

	if (gotRecv && gotSend)
	{
		Pass(name);
	}
	else
	{
		Fail(name, "send/recv completions did not arrive");
	}

	closesocket(serverSock);
	closesocket(clientSock);
	provider.Shutdown();
}

int main()
{
	WinsockGuard winsock;
	if (!winsock.IsReady())
	{
		std::cout << "[FAIL] WinsockInit - WSAStartup failed\n";
		return 1;
	}

	std::cout << "=== IOCP AsyncIOProvider Tests ===\n\n";
	TestIOCPProviderInit();
	TestIOCPLoopbackCompletions();
	std::cout << "\nResult: " << gPassed << " passed, " << gFailed
			  << " failed\n";
	return gFailed > 0 ? 1 : 0;
}

#else // !_WIN32
#include <iostream>
int main()
{
	std::cout << "[SKIP] IOCPTest: Windows only\n";
	return 0;
}
#endif // _WIN32
