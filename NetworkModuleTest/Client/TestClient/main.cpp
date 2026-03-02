// English: TestClient entry point - connects to game server and runs ping/pong
// 한글: TestClient 진입점 - 게임 서버에 접속하여 핑/퐁 실행

#include "Utils/NetworkUtils.h"
#include "include/PlatformInput.h"
#include "include/TestClient.h"
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

using namespace Network::Utils;
using namespace Network::TestClient;

// English: Global client pointer for signal handling
// 한글: 시그널 처리용 전역 클라이언트 포인터
static TestClient *g_pClient = nullptr;

void SignalHandler(int signum)
{
	Logger::Info("Signal received: " + std::to_string(signum));
	if (g_pClient)
	{
		g_pClient->RequestStop();
	}
}

#ifdef _WIN32
// English: Console ctrl handler — catches CTRL_CLOSE_EVENT so client can disconnect cleanly.
// 한글: 콘솔 컨트롤 핸들러 — CTRL_CLOSE_EVENT 처리로 클라이언트가 정상 종료.
static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType)
{
    switch (ctrlType)
    {
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        if (g_pClient)
            g_pClient->RequestStop();
        // English: Give main thread up to 3s to finish Shutdown()
        // 한글: 메인 스레드가 Shutdown()을 완료할 때까지 최대 3초 대기
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
        return TRUE;
    default:
        return FALSE;
    }
}
#endif

void PrintUsage(const char *programName)
{
	std::cout << "Usage: " << programName << " [options]" << std::endl;
	std::cout << "Options:" << std::endl;
	std::cout << "  --host <addr>   Server address (default: 127.0.0.1)"
				  << std::endl;
	std::cout << "  --port <port>   Server port (default: 9000)" << std::endl;
	std::cout << "  --pings <n>     Exit after sending N pings (default: 0 = unlimited)" << std::endl;
	std::cout << "  --clients <n>   Number of client instances (currently ignored)" << std::endl;
	std::cout << "  -l <level>      Log level: DEBUG, INFO, WARN, ERROR "
				 "(default: INFO)"
				  << std::endl;
	std::cout << "  -h, --help      Show this help" << std::endl;
}

void PrintStats(const TestClient &client)
{
	auto stats = client.GetLatencyStats();
	std::cout << std::endl;
	std::cout << "--- Latency Statistics ---" << std::endl;
	std::cout << "  Session ID : " << client.GetSessionId() << std::endl;
	std::cout << "  Ping sent  : " << stats.pingCount << std::endl;
	std::cout << "  Pong recv  : " << stats.pongCount << std::endl;
	if (stats.pongCount > 0)
	{
		std::cout << "  Last RTT   : " << stats.lastRtt << " ms" << std::endl;
		std::cout << "  Min RTT    : " << stats.minRtt << " ms" << std::endl;
		std::cout << "  Max RTT    : " << stats.maxRtt << " ms" << std::endl;
		std::cout << "  Avg RTT    : " << static_cast<int>(stats.avgRtt)
				  << " ms" << std::endl;
	}
	else
	{
		std::cout << "  (no pong received yet)" << std::endl;
	}
	std::cout << "--------------------------" << std::endl;
	std::cout << std::endl;
}

int main(int argc, char *argv[])
{
#ifdef _WIN32
	// English: Set console code page to UTF-8 for Korean output
	// 한글: 한글 출력을 위해 콘솔 코드 페이지를 UTF-8로 설정
	SetConsoleCP(65001);
	SetConsoleOutputCP(65001);
#else
	// English: Enable raw terminal mode for single-key input on POSIX
	// 한글: POSIX에서 단일 키 입력을 위한 raw 터미널 모드 활성화
	EnableRawMode();
#endif

	std::cout << "====================================" << std::endl;
	std::cout << "  TestClient - Network Test Client" << std::endl;
	std::cout << "====================================" << std::endl;

	// English: Default settings
	// 한글: 기본 설정
	std::string host = "127.0.0.1";
	uint16_t port = 9000;
	LogLevel logLevel = LogLevel::Info;
	uint32_t maxPings = 0;

	// English: Parse command line arguments
	// 한글: 커맨드라인 인자 파싱
	for (int i = 1; i < argc; ++i)
	{
		std::string arg = argv[i];
		if (arg == "-h" || arg == "--help")
		{
			PrintUsage(argv[0]);
			return 0;
		}
		else if (arg == "--host" && i + 1 < argc)
		{
			host = argv[++i];
		}
		else if (arg == "--port" && i + 1 < argc)
		{
			port = static_cast<uint16_t>(std::stoi(argv[++i]));
		}
		else if (arg == "-l" && i + 1 < argc)
		{
			std::string level = StringUtils::ToUpper(argv[++i]);
			if (level == "DEBUG")
				logLevel = LogLevel::Debug;
			else if (level == "WARN")
				logLevel = LogLevel::Warn;
			else if (level == "ERROR")
				logLevel = LogLevel::Err;
			// English: Default: Info / 한글: 기본값: Info
		}
		else if (arg == "--pings" && i + 1 < argc)
		{
			maxPings = static_cast<uint32_t>(std::stoi(argv[++i]));
		}
		else if (arg == "--clients" && i + 1 < argc)
		{
			++i; // English: ignored — single-connection mode / 한글: 무시 (단일 연결 모드)
		}
		else
		{
			std::cerr << "Unknown option: " << arg << std::endl;
			PrintUsage(argv[0]);
			return 1;
		}
	}

	Logger::SetLevel(logLevel);

	// English: Register signal handlers
	// 한글: 시그널 핸들러 등록
	std::signal(SIGINT, SignalHandler);
	std::signal(SIGTERM, SignalHandler);
#ifdef _WIN32
	// English: SIGBREAK is Windows-specific (Ctrl+Break console event)
	// 한글: SIGBREAK는 Windows 전용 (Ctrl+Break 콘솔 이벤트)
#ifdef SIGBREAK
	std::signal(SIGBREAK, SignalHandler);
#endif
	// English: Catch CTRL_CLOSE_EVENT for graceful shutdown on window close / taskkill
	// 한글: 창 닫기 / taskkill 시 정상 종료를 위한 ConsoleCtrlHandler 등록
	SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
#endif
	// English: On Linux/macOS SIGINT/SIGTERM are sufficient; SIGBREAK does not exist.
	// 한글: Linux/macOS에서는 SIGINT/SIGTERM으로 충분. SIGBREAK는 존재하지 않음.

	// English: Create and run client
	// 한글: 클라이언트 생성 및 실행
	TestClient client;
	g_pClient = &client;
	if (maxPings > 0)
		client.SetMaxPings(maxPings);

	if (!client.Initialize())
	{
		Logger::Error("Failed to initialize socket platform");
		return 1;
	}

#ifdef _WIN32
	// English: Create a PID-scoped Named Event so test scripts can trigger graceful
	//          shutdown per-instance: "TestClient_GracefulShutdown_<PID>"
	// 한글: PID 기반 Named Event 생성 — 테스트 스크립트가 인스턴스별로 정상 종료 트리거 가능
	//       이름 형식: "TestClient_GracefulShutdown_<PID>"
	const std::string shutdownEventName =
	    "TestClient_GracefulShutdown_" + std::to_string(GetCurrentProcessId());
	HANDLE hShutdownEvent = CreateEventA(nullptr, FALSE, FALSE, shutdownEventName.c_str());
	Logger::Info("Graceful shutdown event: " + shutdownEventName);
#endif

	// English: Reconnect loop - retries on connection loss with exponential backoff
	// 한글: 재연결 루프 - 연결 끊김 시 지수 백오프로 재시도
	constexpr int kMaxReconnectAttempts = 10;
	constexpr uint32_t kMaxReconnectDelayMs = 30000;
	int reconnectAttempt = 0;
	uint32_t reconnectDelay = 1000;

	while (!client.IsStopRequested())
	{
		// ── 연결 (재연결 포함) ─────────────────────────────────────────
		if (!client.IsConnected())
		{
			if (reconnectAttempt > 0)
			{
				Logger::Info("Reconnecting... attempt #" + std::to_string(reconnectAttempt) +
				             " (delay: " + std::to_string(reconnectDelay) + "ms)");
				std::this_thread::sleep_for(std::chrono::milliseconds(reconnectDelay));
				reconnectDelay = std::min(reconnectDelay * 2, kMaxReconnectDelayMs);

				if (client.IsStopRequested()) break;
			}
			else
			{
				Logger::Info("Connecting to " + host + ":" + std::to_string(port) + "...");
			}

			if (!client.Connect(host, port))
			{
				++reconnectAttempt;
				if (reconnectAttempt >= kMaxReconnectAttempts)
				{
					Logger::Error("Max reconnect attempts reached, giving up");
					break;
				}
				continue;
			}

			if (!client.Start())
			{
				Logger::Error("Failed to start network worker");
				break;
			}

			reconnectAttempt = 0;
			reconnectDelay = 1000;

			std::cout << std::endl;
			std::cout << "Connected! Press 'q' to quit, 's' for statistics." << std::endl;
			std::cout << std::endl;
		}

		// ── 메인 루프 ──────────────────────────────────────────────────
		while (!client.IsStopRequested() && client.IsConnected())
		{
#ifdef _WIN32
			// English: Poll Named Event (100ms timeout) — allows test scripts to
			//          signal graceful shutdown without TerminateProcess.
			// 한글: Named Event 폴링 (100ms 타임아웃) — 테스트 스크립트가
			//       TerminateProcess 없이 정상 종료를 트리거할 수 있음.
			if (hShutdownEvent &&
			    WaitForSingleObject(hShutdownEvent, 0) == WAIT_OBJECT_0)
			{
				Logger::Info("Shutdown event signaled - stopping client");
				client.RequestStop();
				break;
			}
#endif
			if (HasKeyInput())
			{
				char ch = ReadKeyChar();
				if (ch == 'q' || ch == 'Q')
				{
					Logger::Info("Quit requested by user");
					client.RequestStop();
					break;
				}
				else if (ch == 's' || ch == 'S')
				{
					PrintStats(client);
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}

		// English: If connection dropped without user request, prepare to reconnect
		// 한글: 사용자 요청 없이 연결이 끊겼으면 재연결 준비
		if (!client.IsStopRequested() && !client.IsConnected())
		{
			Logger::Warn("Connection lost - will reconnect");
			client.Disconnect();
			++reconnectAttempt;
			if (reconnectAttempt >= kMaxReconnectAttempts)
			{
				Logger::Error("Max reconnect attempts reached");
				break;
			}
		}
	}

	// English: Print final stats
	// 한글: 최종 통계 출력
	PrintStats(client);

	// English: Graceful shutdown
	// 한글: 정상 종료
	client.Shutdown();
	g_pClient = nullptr;

#ifdef _WIN32
	if (hShutdownEvent) CloseHandle(hShutdownEvent);
#endif

#ifndef _WIN32
	// English: Restore terminal mode before exit
	// 한글: 종료 전 터미널 모드 복원
	RestoreTerminal();
#endif

	Logger::Info("TestClient shutdown complete.");
	return 0;
}
