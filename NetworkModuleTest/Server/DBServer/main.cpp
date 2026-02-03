// English: TestDBServer entry point
// 한글: TestDBServer 진입점

#include "include/DBServer.h"
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

static std::atomic<bool> gRunning{true};

void SignalHandler(int signum)
{
	(void)signum;
	gRunning = false;
}

void PrintUsage(const char *programName)
{
	std::cout << "Usage: " << programName << " [options]" << std::endl;
	std::cout << "Options:" << std::endl;
	std::cout << "  -p <port>       Server port (default: 8002)" << std::endl;
	std::cout << "  -m <max>        Max connections (default: 1000)"
			  << std::endl;
	std::cout << "  -h              Show this help" << std::endl;
}

int main(int argc, char *argv[])
{
	uint16_t port = 8002;
	size_t maxConnections = 1000;

	for (int i = 1; i < argc; ++i)
	{
		const std::string arg = argv[i];
		if (arg == "-h" || arg == "--help")
		{
			PrintUsage(argv[0]);
			return 0;
		}
		if (arg == "-p" && i + 1 < argc)
		{
			port = static_cast<uint16_t>(std::stoi(argv[++i]));
			continue;
		}
		if (arg == "-m" && i + 1 < argc)
		{
			maxConnections = static_cast<size_t>(std::stoul(argv[++i]));
			continue;
		}

		std::cerr << "Unknown option: " << arg << std::endl;
		PrintUsage(argv[0]);
		return 1;
	}

	Network::DBServer::DBServer server;

	// 한글: 신호 처리로 안전하게 종료할 수 있도록 설정한다.
	std::signal(SIGINT, SignalHandler);
	std::signal(SIGTERM, SignalHandler);

	if (!server.Initialize(port, maxConnections))
	{
		std::cerr << "Failed to initialize DBServer" << std::endl;
		return 1;
	}

	if (!server.Start())
	{
		std::cerr << "Failed to start DBServer" << std::endl;
		return 1;
	}

	std::cout << "DBServer running on port " << port << std::endl;

	while (gRunning && server.IsRunning())
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	// 한글: 종료 요청 시 서버를 정상적으로 정리한다.
	server.Stop();

	return 0;
}
