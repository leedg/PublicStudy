// TestServer 진입점 - 게임 서버 초기화 및 실행

#include "Utils/NetworkUtils.h"
#include "Utils/CrashDump.h"
#include "include/TestServer.h"
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

// 시그널 처리용 전역 서버 인스턴스
static Network::TestServer::TestServer *g_pServer = nullptr;
static std::atomic<bool> g_Running{true};
static std::atomic<bool> g_ShutdownComplete{false};

// 정상 종료를 위한 시그널 핸들러 (SIGINT/SIGTERM)
void SignalHandler(int signum)
{
	Network::Utils::Logger::Info("Signal received: " + std::to_string(signum));
	g_Running = false;
}

#ifdef _WIN32
// 콘솔 컨트롤 핸들러 - taskkill/창 닫기의 CTRL_CLOSE_EVENT 처리.
// 프로세스 종료 전에 server.Stop()이 호출되도록 보장하여
// DB 소켓의 WSAECONNRESET(10054) 방지.
static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType)
{
	switch (ctrlType)
	{
	case CTRL_CLOSE_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		Network::Utils::Logger::Info("Console shutdown event received (" +
		                             std::to_string(ctrlType) + "), stopping server...");
		g_Running = false;
		// 메인 스레드가 server.Stop()을 완료할 때까지 최대 8초 대기
		for (int i = 0; i < 80 && !g_ShutdownComplete.load(); ++i)
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		return TRUE;
	default:
		return FALSE;  // CTRL_C_EVENT/CTRL_BREAK_EVENT: let std::signal handle
	}
}
#endif

// 사용법 출력
void PrintUsage(const char *programName)
{
	std::cout << "Usage: " << programName << " [options]" << std::endl;
	std::cout << "Options:" << std::endl;
	std::cout << "  -p <port>       Server port (default: " << Network::Utils::DEFAULT_TEST_SERVER_PORT << ")" << std::endl;
	std::cout << "  -d <connstr>    DB connection string (optional)"
				  << std::endl;
	std::cout << "  --db            Connect to DB server (default: 127.0.0.1:" << Network::Utils::DEFAULT_TEST_DB_PORT << ")" << std::endl;
	std::cout << "  --db-host <h>   DB server host" << std::endl;
	std::cout << "  --db-port <p>   DB server port" << std::endl;
	std::cout << "  --engine <name> Network engine (default: auto)" << std::endl;
	std::cout << "  -w <count>      DB task queue worker threads (default: " << Network::Utils::DEFAULT_TASK_QUEUE_WORKER_COUNT << ")" << std::endl;
	std::cout << "  -l <level>      Log level: DEBUG, INFO, WARN, ERROR (default: INFO)" << std::endl;
	std::cout << "  --self-test     Run DBServerTaskQueue self-test then exit" << std::endl;
	std::cout << "  -h              Show this help" << std::endl;
}

// 로그 레벨 문자열 파싱
Network::Utils::LogLevel ParseLogLevel(const std::string &level)
{
	std::string upper = Network::Utils::StringUtils::ToUpper(level);
	if (upper == "DEBUG")
		return Network::Utils::LogLevel::Debug;
	if (upper == "WARN")
		return Network::Utils::LogLevel::Warn;
	if (upper == "ERROR")
		return Network::Utils::LogLevel::Err;
	return Network::Utils::LogLevel::Info;
}

int main(int argc, char *argv[])
{
#ifdef _WIN32
	// 한글 출력을 위해 콘솔 코드 페이지를 UTF-8로 설정
	SetConsoleCP(65001);
	SetConsoleOutputCP(65001);

	// 크래시 덤프 핸들러 설치 — 미처리 예외 발생 시 .dmp + .crash 파일 기록
	Network::Utils::CrashDump::Initialize("./dumps/");
#endif

	std::cout << "====================================" << std::endl;
	std::cout << "  TestServer - IOCP Game Server" << std::endl;
	std::cout << "====================================" << std::endl;

	// 기본 설정
	uint16_t    port          = Network::Utils::DEFAULT_TEST_SERVER_PORT;
	size_t      dbWorkerCount = Network::Utils::DEFAULT_TASK_QUEUE_WORKER_COUNT;
	std::string dbConnectionString;
	Network::Utils::LogLevel logLevel = Network::Utils::LogLevel::Info;
	bool        dbConnectRequested = false;
	std::string dbHost             = "127.0.0.1";
	uint16_t    dbPort             = Network::Utils::DEFAULT_TEST_DB_PORT;
	std::string engineType         = "auto";
	bool        selfTest           = false;

	// 커맨드라인 인자 파싱
	for (int i = 1; i < argc; ++i)
	{
		std::string arg = argv[i];

		if (arg == "-h" || arg == "--help")
		{
			PrintUsage(argv[0]);
			return 0;
		}
		else if (arg == "-p" && i + 1 < argc)
		{
			try
			{
				int val = std::stoi(argv[++i]);
				if (val < 1 || val > 65535)
				{
					std::cerr << "Error: port must be in range 1-65535" << std::endl;
					return 1;
				}
				port = static_cast<uint16_t>(val);
			}
			catch (const std::exception &)
			{
				std::cerr << "Error: invalid port value: " << argv[i] << std::endl;
				return 1;
			}
		}
		else if (arg == "-d" && i + 1 < argc)
		{
			dbConnectionString = argv[++i];
		}
		else if (arg == "-l" && i + 1 < argc)
		{
			logLevel = ParseLogLevel(argv[++i]);
		}
		else if (arg == "--db")
		{
			dbConnectRequested = true;
		}
		else if (arg == "--db-host" && i + 1 < argc)
		{
			dbHost = argv[++i];
			dbConnectRequested = true;
		}
		else if (arg == "--db-port" && i + 1 < argc)
		{
			try
			{
				int val = std::stoi(argv[++i]);
				if (val < 1 || val > 65535)
				{
					std::cerr << "Error: --db-port must be in range 1-65535" << std::endl;
					return 1;
				}
				dbPort = static_cast<uint16_t>(val);
			}
			catch (const std::exception &)
			{
				std::cerr << "Error: invalid --db-port value: " << argv[i] << std::endl;
				return 1;
			}
			dbConnectRequested = true;
		}
		else if (arg == "--engine" && i + 1 < argc)
		{
			engineType = argv[++i];
		}
		else if (arg == "-w" && i + 1 < argc)
		{
			try
			{
				int val = std::stoi(argv[++i]);
				if (val < 1 || val > 64)
				{
					std::cerr << "Error: worker count must be in range 1-64" << std::endl;
					return 1;
				}
				dbWorkerCount = static_cast<size_t>(val);
			}
			catch (const std::exception &)
			{
				std::cerr << "Error: invalid worker count value: " << argv[i] << std::endl;
				return 1;
			}
		}
		else if (arg == "--self-test")
		{
			selfTest = true;
		}
		else
		{
			std::cerr << "Unknown option: " << arg << std::endl;
			PrintUsage(argv[0]);
			return 1;
		}
	}

	// 로깅 설정
	Network::Utils::Logger::SetLevel(logLevel);

	// 시그널 핸들러 등록
	std::signal(SIGINT, SignalHandler);
	std::signal(SIGTERM, SignalHandler);
#ifdef _WIN32
// SIGBREAK는 Windows 전용, <signal.h>에 정의
#ifdef SIGBREAK
	std::signal(SIGBREAK, SignalHandler);
#endif
	// CTRL_CLOSE_EVENT (taskkill /T, 창 닫기) 처리하여 정상 종료
	SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
#endif

	// 서버 생성 및 초기화
	Network::TestServer::TestServer server;
	g_pServer = &server;

	Network::Utils::Logger::Info("Initializing server on port " +
								 std::to_string(port));

	if (!server.Initialize(port, dbConnectionString, engineType, dbWorkerCount))
	{
		Network::Utils::Logger::Error("Failed to initialize server");
		return 1;
	}

	// --self-test 옵션 시 셀프 테스트 후 종료
	if (selfTest)
	{
		const bool ok = server.RunSelfTest();
		server.Stop();
		return ok ? 0 : 1;
	}

	// 서버 시작
	if (!server.Start())
	{
		Network::Utils::Logger::Error("Failed to start server");
		return 1;
	}

	if (dbConnectRequested)
	{
		if (!server.ConnectToDBServer(dbHost, dbPort))
		{
			Network::Utils::Logger::Warn("DB server connection failed - continuing without DB link");
		}
	}

	Network::Utils::Logger::Info("Server is running. Press Ctrl+C to stop.");

	// 메인 루프 - 종료 시그널 또는 Named Event 대기
#ifdef _WIN32
	// Named Event로 테스트 스크립트 등 외부 도구가 콘솔 없이도 정상 종료 가능
	HANDLE hShutdownEvent = CreateEventA(nullptr, FALSE, FALSE, "TestServer_GracefulShutdown");
	while (g_Running && server.IsRunning())
	{
		if (hShutdownEvent && WaitForSingleObject(hShutdownEvent, 100) == WAIT_OBJECT_0)
		{
			Network::Utils::Logger::Info("Shutdown event signaled - stopping server");
			g_Running = false;
		}
	}
	if (hShutdownEvent) CloseHandle(hShutdownEvent);
#else
	while (g_Running && server.IsRunning())
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
#endif

	// 정상 종료
	Network::Utils::Logger::Info("Shutting down server...");
	server.Stop();
	g_pServer = nullptr;

	// 정리 완료를 ConsoleCtrlHandler에 알림 (대기 중인 경우)
	g_ShutdownComplete.store(true);

	Network::Utils::Logger::Info("Server stopped.");
	std::cout << "Server shutdown complete." << std::endl;

	return 0;
}
