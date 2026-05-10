// English: TestServer entry point - initializes and runs the game server
// 한글: TestServer 진입점 - 게임 서버 초기화 및 실행

#include "Utils/ConfigManager.h"
#include "Utils/CrashDump.h"
#include "Utils/NetworkUtils.h"
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

// English: Global server instance for signal handling
// 한글: 시그널 처리용 전역 서버 인스턴스
static Network::TestServer::TestServer *g_pServer = nullptr;
static std::atomic<bool> g_Running{true};
static std::atomic<bool> g_ShutdownComplete{false};

// English: Signal handler for graceful shutdown (SIGINT/SIGTERM)
// 한글: 정상 종료를 위한 시그널 핸들러 (SIGINT/SIGTERM)
void SignalHandler(int signum)
{
	Network::Utils::Logger::Info("Signal received: " + std::to_string(signum));
	g_Running = false;
}

#ifdef _WIN32
// English: Console ctrl handler - catches CTRL_CLOSE_EVENT from taskkill/window close
//          so that server.Stop() (DisconnectFromDBServer) runs before process exits,
//          preventing WSAECONNRESET(10054) on the DB socket.
// 한글: 콘솔 컨트롤 핸들러 - taskkill/창 닫기의 CTRL_CLOSE_EVENT 처리
//       프로세스 종료 전에 server.Stop()이 호출되도록 보장하여
//       DB 소켓의 WSAECONNRESET(10054) 방지
static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType)
{
	if (ctrlType == CTRL_CLOSE_EVENT ||
		ctrlType == CTRL_LOGOFF_EVENT ||
		ctrlType == CTRL_SHUTDOWN_EVENT)
	{
		Network::Utils::Logger::Info("Console shutdown event received (" +
		                             std::to_string(ctrlType) + "), stopping server...");
		g_Running = false;
		// English: Wait up to gracefulShutdownTimeoutMs for main thread to finish server.Stop()
		// 한글: 메인 스레드가 server.Stop()을 완료할 때까지 gracefulShutdownTimeoutMs 대기
		const uint32_t shutdownTimeoutMs = Network::Utils::ConfigManager::Instance().GetTimeout().GracefulShutdownTimeoutMs;
		const int waitCount = static_cast<int>(shutdownTimeoutMs / 100);
		for (int i = 0; i < waitCount && !g_ShutdownComplete.load(); ++i)
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		return TRUE;
	}
	return FALSE;  // CTRL_C_EVENT/CTRL_BREAK_EVENT: let std::signal handle
}
#endif

// English: Print usage information
// 한글: 사용법 출력
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
	std::cout << "  -l <level>      Log level: DEBUG, INFO, WARN, ERROR "
				 "(default: INFO)"
				  << std::endl;
	std::cout << "  -h              Show this help" << std::endl;
	std::cout << std::endl;
	std::cout << "Environment Variables (override defaults):" << std::endl;
	std::cout << "  NETMOD_LISTEN_PORT        Server listen port" << std::endl;
	std::cout << "  NETMOD_DB_HOST           DB server host" << std::endl;
	std::cout << "  NETMOD_DB_PORT           DB server port" << std::endl;
	std::cout << "  NETMOD_ENGINE            Network engine (auto/rio/iocp/epoll/kqueue)" << std::endl;
	std::cout << "  NETMOD_WORKER_THREADS    Worker thread count (0=auto)" << std::endl;
	std::cout << "  NETMOD_LOG_LEVEL         Log level (DEBUG/INFO/WARN/ERROR)" << std::endl;
	std::cout << "  NETMOD_GRACEFUL_TIMEOUT  Shutdown timeout in seconds" << std::endl;
}

// English: Parse log level string
// 한글: 로그 레벨 문자열 파싱
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
	// English: Set console code page to UTF-8 for Korean output
	// 한글: 한글 출력을 위해 콘솔 코드 페이지를 UTF-8로 설정
	SetConsoleCP(65001);
	SetConsoleOutputCP(65001);

	// English: Install crash dump handler — writes .dmp + .crash on unhandled exception
	// 한글: 크래시 덤프 핸들러 설치 — 미처리 예외 발생 시 .dmp + .crash 파일 기록
	Network::Utils::CrashDump::Initialize("./dumps/");
#endif

	std::cout << "====================================" << std::endl;
	std::cout << "  TestServer - IOCP Game Server" << std::endl;
	std::cout << "====================================" << std::endl;

	// English: Initialize ConfigManager (loads defaults + env overrides)
	// 한글: ConfigManager 초기화 (기본값 로드 + 환경 변수 덮어쓰기)
	Network::Utils::ConfigManager::Instance().Initialize();

	// English: Default settings (may be overridden by ConfigManager from env)
	// 한글: 기본 설정 (ConfigManager/환경 변수로 덮어쓰기 가능)
	uint16_t port = Network::Utils::ConfigManager::Instance().GetNetwork().ListenPort;
	std::string dbConnectionString;
	Network::Utils::LogLevel logLevel = Network::Utils::LogLevel::Info;
	bool dbConnectRequested = false;
	std::string dbHost = Network::Utils::ConfigManager::Instance().GetNetwork().DBServerHost;
	uint16_t dbPort = Network::Utils::ConfigManager::Instance().GetNetwork().DBServerPort;
	std::string engineType = Network::Utils::ConfigManager::Instance().GetNetwork().EngineType;

	// English: Parse command line arguments (override ConfigManager settings)
	// 한글: 커맨드라인 인자 파싱 (ConfigManager 설정 덮어쓰기)
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
			port = static_cast<uint16_t>(std::stoi(argv[++i]));
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
			dbPort = static_cast<uint16_t>(std::stoi(argv[++i]));
			dbConnectRequested = true;
		}
		else if (arg == "--engine" && i + 1 < argc)
		{
			engineType = argv[++i];
		}
		else
		{
			std::cerr << "Unknown option: " << arg << std::endl;
			PrintUsage(argv[0]);
			return 1;
		}
	}

	// English: Setup logging
	// 한글: 로깅 설정
	Network::Utils::Logger::SetLevel(logLevel);

	// English: Print current configuration
	// 한글: 현재 설정 출력
	Network::Utils::ConfigManager::Instance().PrintConfig();

	// English: Register signal handlers
	// 한글: 시그널 핸들러 등록
	std::signal(SIGINT, SignalHandler);
	std::signal(SIGTERM, SignalHandler);
#ifdef _WIN32
	// English: SIGBREAK is Windows-specific, defined in <signal.h>
	// 한글: SIGBREAK는 Windows 전용, <signal.h>에 정의
#ifdef SIGBREAK
	std::signal(SIGBREAK, SignalHandler);
#endif
	// English: Catch CTRL_CLOSE_EVENT (taskkill /T, window close) for graceful shutdown
	// 한글: CTRL_CLOSE_EVENT (taskkill /T, 창 닫기) 처리하여 정상 종료
	SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
#endif

	// English: Create and initialize server
	// 한글: 서버 생성 및 초기화
	Network::TestServer::TestServer server;
	g_pServer = &server;

	Network::Utils::Logger::Info("Initializing server on port " +
								 std::to_string(port));

	if (!server.Initialize(port, dbConnectionString, engineType))
	{
		Network::Utils::Logger::Error("Failed to initialize server");
		return 1;
	}

	// English: Start server
	// 한글: 서버 시작
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

	// English: Get graceful shutdown timeout from ConfigManager
	// 한글: ConfigManager에서 정상 종료 타임아웃 가져오기
	const uint32_t shutdownTimeoutMs = Network::Utils::ConfigManager::Instance().GetTimeout().GracefulShutdownTimeoutMs;
	const int shutdownTimeoutWaitCount = static_cast<int>(shutdownTimeoutMs / 100);

	// English: Main loop - wait for shutdown signal (signal or named event)
	// 한글: 메인 루프 - 종료 시그널 또는 Named Event 대기
#ifdef _WIN32
	// English: Named event allows external tools (e.g. test scripts) to trigger
	//          graceful shutdown without console manipulation
	// 한글: Named Event로 테스트 스크립트 등 외부 도구가 콘솔 없이도 정상 종료 가능
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

	// English: Graceful shutdown
	// 한글: 정상 종료
	Network::Utils::Logger::Info("Shutting down server...");
	server.Stop();
	g_pServer = nullptr;

	// English: Signal ConsoleCtrlHandler (if waiting) that cleanup is done
	// 한글: 정리 완료를 ConsoleCtrlHandler에 알림 (대기 중인 경우)
	g_ShutdownComplete.store(true);

	Network::Utils::Logger::Info("Server stopped.");
	std::cout << "Server shutdown complete." << std::endl;

	return 0;
}
