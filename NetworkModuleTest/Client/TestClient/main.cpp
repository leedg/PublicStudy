// English: TestClient entry point - connects to game server and runs ping/pong
// 한글: TestClient 진입점 - 게임 서버에 접속하여 핑/퐁 실행

#include "Utils/NetworkUtils.h"
#include "include/TestClient.h"
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

// =============================================================================
// English: Platform-specific keyboard input headers
// 한글: 플랫폼별 키보드 입력 헤더
// =============================================================================
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <conio.h>
#include <windows.h>
#else
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#include <cstdlib>
#endif

using namespace Network::Utils;
using namespace Network::TestClient;

// =============================================================================
// English: Cross-platform keyboard input helpers
// 한글: 크로스 플랫폼 키보드 입력 헬퍼
// =============================================================================
#ifndef _WIN32
namespace
{

// English: Original terminal attributes for restoration on exit
// 한글: 종료 시 복원을 위한 원래 터미널 속성
static struct termios g_OrigTermios;
static bool g_TermiosModified = false;

// English: Restore terminal to original mode (called on exit)
// 한글: 터미널을 원래 모드로 복원 (종료 시 호출)
void RestoreTerminal()
{
	if (g_TermiosModified)
	{
		tcsetattr(STDIN_FILENO, TCSANOW, &g_OrigTermios);
		g_TermiosModified = false;
	}
}

// English: Enable raw mode for single-character input without echo
// 한글: 에코 없이 단일 문자 입력을 위한 raw 모드 활성화
void EnableRawMode()
{
	if (g_TermiosModified)
	{
		return; // English: Already in raw mode / 한글: 이미 raw 모드
	}

	tcgetattr(STDIN_FILENO, &g_OrigTermios);
	g_TermiosModified = true;

	// English: Register atexit handler to restore terminal on abnormal exit
	// 한글: 비정상 종료 시 터미널 복원을 위한 atexit 핸들러 등록
	std::atexit(RestoreTerminal);

	struct termios raw = g_OrigTermios;
	// English: Disable canonical mode and echo
	// 한글: 정규 모드와 에코 비활성화
	raw.c_lflag &= ~(ICANON | ECHO);
	// English: Read returns immediately with available bytes (non-blocking feel)
	// 한글: 읽기가 사용 가능한 바이트로 즉시 반환 (논블로킹 느낌)
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 0;
	tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

// English: Check if keyboard input is available (POSIX equivalent of _kbhit)
// 한글: 키보드 입력이 있는지 확인 (POSIX용 _kbhit 대체)
bool PosixKbhit()
{
	struct timeval tv = {0, 0};
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(STDIN_FILENO, &fds);
	return select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0;
}

// English: Read a single character without echo (POSIX equivalent of _getch)
// 한글: 에코 없이 단일 문자 읽기 (POSIX용 _getch 대체)
char PosixGetch()
{
	char ch = 0;
	if (read(STDIN_FILENO, &ch, 1) == 1)
	{
		return ch;
	}
	return 0;
}

} // anonymous namespace
#endif // !_WIN32

// English: Unified cross-platform keyboard input functions
// 한글: 통합 크로스 플랫폼 키보드 입력 함수
#ifdef _WIN32
inline bool HasKeyInput() { return _kbhit() != 0; }
inline char ReadKeyChar() { return static_cast<char>(_getch()); }
#else
inline bool HasKeyInput() { return PosixKbhit(); }
inline char ReadKeyChar() { return PosixGetch(); }
#endif

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

void PrintUsage(const char *programName)
{
	std::cout << "Usage: " << programName << " [options]" << std::endl;
	std::cout << "Options:" << std::endl;
	std::cout << "  --host <addr>   Server address (default: 127.0.0.1)"
				  << std::endl;
	std::cout << "  --port <port>   Server port (default: 9000)" << std::endl;
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
#ifdef SIGBREAK
	std::signal(SIGBREAK, SignalHandler);
#endif

	// English: Create and run client
	// 한글: 클라이언트 생성 및 실행
	TestClient client;
	g_pClient = &client;

	if (!client.Initialize())
	{
		Logger::Error("Failed to initialize socket platform");
		return 1;
	}

	Logger::Info("Connecting to " + host + ":" + std::to_string(port) + "...");

	if (!client.Connect(host, port))
	{
		Logger::Error("Failed to connect to server");
		client.Shutdown();
		return 1;
	}

	if (!client.Start())
	{
		Logger::Error("Failed to start network worker");
		client.Shutdown();
		return 1;
	}

	std::cout << std::endl;
	std::cout << "Connected! Press 'q' to quit, 's' for statistics."
				  << std::endl;
	std::cout << std::endl;

	// English: Main loop - user input handling (cross-platform)
	// 한글: 메인 루프 - 사용자 입력 처리 (크로스 플랫폼)
	while (!client.IsStopRequested() && client.IsConnected())
	{
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

	// English: Check if disconnected unexpectedly
	// 한글: 예기치 않은 연결 해제 확인
	if (!client.IsConnected() && !client.IsStopRequested())
	{
		Logger::Warn("Connection lost");
	}

	// English: Print final stats
	// 한글: 최종 통계 출력
	PrintStats(client);

	// English: Graceful shutdown
	// 한글: 정상 종료
	client.Shutdown();
	g_pClient = nullptr;

#ifndef _WIN32
	// English: Restore terminal mode before exit
	// 한글: 종료 전 터미널 모드 복원
	RestoreTerminal();
#endif

	Logger::Info("TestClient shutdown complete.");
	return 0;
}
