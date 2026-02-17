// English: TestDBServer entry point - initializes and runs the database server
// Korean: TestDBServer 진입점 - 데이터베이스 서버 초기화 및 실행

#include "include/TestDBServer.h"
#include "Utils/NetworkUtils.h"
#include "Utils/CrashDump.h"
#include <iostream>
#include <string>
#include <csignal>
#include <thread>
#include <chrono>
#include <atomic>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

// English: Global server instance for signal handling
// Korean: 시그널 처리용 전역 서버 인스턴스
static Network::DBServer::TestDBServer* g_pServer = nullptr;
static std::atomic<bool> g_Running{ true };
static std::atomic<bool> g_ShutdownComplete{ false };

// English: Signal handler for graceful shutdown
// Korean: 정상 종료를 위한 시그널 핸들러
void SignalHandler(int signum)
{
    Network::Utils::Logger::Info("Signal received: " + std::to_string(signum));
    g_Running = false;
}

#ifdef _WIN32
// English: Console ctrl handler — catches CTRL_CLOSE_EVENT / taskkill so server.Stop() runs.
// 한글: 콘솔 컨트롤 핸들러 — CTRL_CLOSE_EVENT/창 닫기 시 server.Stop()이 실행되도록 보장.
static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType)
{
    switch (ctrlType)
    {
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        Network::Utils::Logger::Info("Console shutdown event received (" +
                                     std::to_string(ctrlType) + "), stopping DBServer...");
        g_Running = false;
        // English: Wait up to 8s for main thread to finish server.Stop()
        // 한글: 메인 스레드가 server.Stop()을 완료할 때까지 최대 8초 대기
        for (int i = 0; i < 80 && !g_ShutdownComplete.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return TRUE;
    default:
        return FALSE;  // CTRL_C_EVENT / CTRL_BREAK_EVENT: std::signal이 처리
    }
}
#endif

// English: Print usage information
// Korean: 사용법 출력
void PrintUsage(const char* programName)
{
    std::cout << "Usage: " << programName << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -p <port>       Server port (default: 8001)" << std::endl;
    std::cout << "  -l <level>      Log level: DEBUG, INFO, WARN, ERROR (default: INFO)" << std::endl;
    std::cout << "  -h              Show this help" << std::endl;
}

// English: Parse log level string
// Korean: 로그 레벨 문자열 파싱
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
#ifdef _WIN32
    // English: Set console code page to UTF-8 for Korean output
    // Korean: 한글 출력을 위해 콘솔 코드 페이지를 UTF-8로 설정
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);

    // English: Install crash dump handler — writes .dmp + .crash on unhandled exception
    // 한글: 크래시 덤프 핸들러 설치 — 미처리 예외 발생 시 .dmp + .crash 파일 기록
    Network::Utils::CrashDump::Initialize("./dumps/");
#endif

    std::cout << "====================================" << std::endl;
    std::cout << "  TestDBServer - Database Server" << std::endl;
    std::cout << "====================================" << std::endl;

    // English: Default settings
    // Korean: 기본 설정
    uint16_t port = 8001;
    Network::Utils::LogLevel logLevel = Network::Utils::LogLevel::Info;

    // English: Parse command line arguments
    // Korean: 커맨드라인 인자 파싱
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
    // Korean: 로깅 설정
    Network::Utils::Logger::SetLevel(logLevel);

    // English: Register signal handlers
    // Korean: 시그널 핸들러 등록
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
#ifdef _WIN32
    // English: SIGBREAK is Windows-specific
    // Korean: SIGBREAK는 Windows 전용
    #ifdef SIGBREAK
    std::signal(SIGBREAK, SignalHandler);
    #endif
    // English: Catch CTRL_CLOSE_EVENT for graceful shutdown on window close / taskkill
    // 한글: 창 닫기 / taskkill 시 정상 종료를 위한 ConsoleCtrlHandler 등록
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
#endif

    // English: Create and initialize server
    // Korean: 서버 생성 및 초기화
    Network::DBServer::TestDBServer server;
    g_pServer = &server;

    Network::Utils::Logger::Info("Initializing TestDBServer on port " + std::to_string(port));

    if (!server.Initialize(port))
    {
        Network::Utils::Logger::Error("Failed to initialize server");
        return 1;
    }

    // English: Start server
    // Korean: 서버 시작
    if (!server.Start())
    {
        Network::Utils::Logger::Error("Failed to start server");
        return 1;
    }

    Network::Utils::Logger::Info("TestDBServer is running. Press Ctrl+C to stop.");

    // English: Main loop — waits for SIGINT/SIGTERM, ConsoleCtrlHandler, or Named Event.
    //          Named event "TestDBServer_GracefulShutdown" allows test scripts to trigger
    //          graceful shutdown without console manipulation (no TerminateProcess).
    // 한글: 메인 루프 — SIGINT/SIGTERM, ConsoleCtrlHandler, 또는 Named Event 대기.
    //       Named Event "TestDBServer_GracefulShutdown"으로 테스트 스크립트가
    //       콘솔 없이 정상 종료를 트리거할 수 있음 (TerminateProcess 불필요).
#ifdef _WIN32
    HANDLE hShutdownEvent = CreateEventA(nullptr, FALSE, FALSE, "TestDBServer_GracefulShutdown");
    while (g_Running && server.IsRunning())
    {
        if (hShutdownEvent && WaitForSingleObject(hShutdownEvent, 100) == WAIT_OBJECT_0)
        {
            Network::Utils::Logger::Info("Shutdown event signaled - stopping DBServer");
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
    // Korean: 정상 종료
    Network::Utils::Logger::Info("Shutting down TestDBServer...");
    server.Stop();
    g_pServer = nullptr;

    // English: Signal ConsoleCtrlHandler (if waiting) that cleanup is done
    // 한글: 정리 완료를 ConsoleCtrlHandler에 알림
    g_ShutdownComplete.store(true);

    Network::Utils::Logger::Info("TestDBServer stopped.");
    std::cout << "Server shutdown complete." << std::endl;

    return 0;
}
