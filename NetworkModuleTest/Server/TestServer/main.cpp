// English: TestServer entry point - initializes and runs the game server
// 한글: TestServer 진입점 - 게임 서버 초기화 및 실행

#include "include/TestServer.h"
#include "Utils/NetworkUtils.h"
#include <iostream>
#include <string>
#include <csignal>
#include <thread>
#include <chrono>
#include <atomic>

// English: Global server instance for signal handling
// 한글: 시그널 처리용 전역 서버 인스턴스
static Network::TestServer::TestServer* g_pServer = nullptr;
static std::atomic<bool> g_Running{ true };

// English: Signal handler for graceful shutdown
// 한글: 정상 종료를 위한 시그널 핸들러
void SignalHandler(int signum)
{
    Network::Utils::Logger::Info("Signal received: " + std::to_string(signum));
    g_Running = false;
}

// English: Print usage information
// 한글: 사용법 출력
void PrintUsage(const char* programName)
{
    std::cout << "Usage: " << programName << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -p <port>       Server port (default: 9000)" << std::endl;
    std::cout << "  -d <connstr>    DB connection string (optional)" << std::endl;
    std::cout << "  -l <level>      Log level: DEBUG, INFO, WARN, ERROR (default: INFO)" << std::endl;
    std::cout << "  -h              Show this help" << std::endl;
}

// English: Parse log level string
// 한글: 로그 레벨 문자열 파싱
Network::Utils::LogLevel ParseLogLevel(const std::string& level)
{
    std::string upper = Network::Utils::StringUtils::ToUpper(level);
    if (upper == "DEBUG") return Network::Utils::LogLevel::Debug;
    if (upper == "WARN")  return Network::Utils::LogLevel::Warn;
    if (upper == "ERROR") return Network::Utils::LogLevel::Err;
    return Network::Utils::LogLevel::Info;
}

int main(int argc, char* argv[])
{
    std::cout << "====================================" << std::endl;
    std::cout << "  TestServer - IOCP Game Server" << std::endl;
    std::cout << "====================================" << std::endl;

    // English: Default settings
    // 한글: 기본 설정
    uint16_t port = 9000;
    std::string dbConnectionString;
    Network::Utils::LogLevel logLevel = Network::Utils::LogLevel::Info;

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
#endif

    // English: Create and initialize server
    // 한글: 서버 생성 및 초기화
    Network::TestServer::TestServer server;
    g_pServer = &server;

    Network::Utils::Logger::Info("Initializing server on port " + std::to_string(port));

    if (!server.Initialize(port, dbConnectionString))
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

    Network::Utils::Logger::Info("Server is running. Press Ctrl+C to stop.");

    // English: Main loop - wait for shutdown signal
    // 한글: 메인 루프 - 종료 시그널 대기
    while (g_Running && server.IsRunning())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // English: Graceful shutdown
    // 한글: 정상 종료
    Network::Utils::Logger::Info("Shutting down server...");
    server.Stop();
    g_pServer = nullptr;

    Network::Utils::Logger::Info("Server stopped.");
    std::cout << "Server shutdown complete." << std::endl;

    return 0;
}
