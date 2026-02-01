// English: TestServer entry point - initializes and runs the game server
// ?쒓?: TestServer 吏꾩엯??- 寃뚯엫 ?쒕쾭 珥덇린??諛??ㅽ뻾

#include "include/TestServer.h"
#include "Utils/NetworkUtils.h"
#include <iostream>
#include <string>
#include <csignal>
#include <thread>
#include <chrono>
#include <atomic>

// English: Global server instance for signal handling
// ?쒓?: ?쒓렇??泥섎━???꾩뿭 ?쒕쾭 ?몄뒪?댁뒪
static Network::TestServer::TestServer* g_pServer = nullptr;
static std::atomic<bool> g_Running{ true };

// English: Signal handler for graceful shutdown
// ?쒓?: ?뺤긽 醫낅즺瑜??꾪븳 ?쒓렇???몃뱾??
void SignalHandler(int signum)
{
    Network::Utils::Logger::Info("Signal received: " + std::to_string(signum));
    g_Running = false;
}

// English: Print usage information
// ?쒓?: ?ъ슜踰?異쒕젰
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
// ?쒓?: 濡쒓렇 ?덈꺼 臾몄옄???뚯떛
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
    // ?쒓?: 湲곕낯 ?ㅼ젙
    uint16_t port = 9000;
    std::string dbConnectionString;
    Network::Utils::LogLevel logLevel = Network::Utils::LogLevel::Info;

    // English: Parse command line arguments
    // ?쒓?: 而ㅻ㎤?쒕씪???몄옄 ?뚯떛
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
    // ?쒓?: 濡쒓퉭 ?ㅼ젙
    Network::Utils::Logger::SetLevel(logLevel);

    // English: Register signal handlers
    // ?쒓?: ?쒓렇???몃뱾???깅줉
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
#ifdef _WIN32
    // English: SIGBREAK is Windows-specific, defined in <signal.h>
    // ?쒓?: SIGBREAK??Windows ?꾩슜, <signal.h>???뺤쓽
    #ifdef SIGBREAK
    std::signal(SIGBREAK, SignalHandler);
    #endif
#endif

    // English: Create and initialize server
    // ?쒓?: ?쒕쾭 ?앹꽦 諛?珥덇린??
    Network::TestServer::TestServer server;
    g_pServer = &server;

    Network::Utils::Logger::Info("Initializing server on port " + std::to_string(port));

    if (!server.Initialize(port, dbConnectionString))
    {
        Network::Utils::Logger::Error("Failed to initialize server");
        return 1;
    }

    // English: Start server
    // ?쒓?: ?쒕쾭 ?쒖옉
    if (!server.Start())
    {
        Network::Utils::Logger::Error("Failed to start server");
        return 1;
    }

    Network::Utils::Logger::Info("Server is running. Press Ctrl+C to stop.");

    // English: Main loop - wait for shutdown signal
    // ?쒓?: 硫붿씤 猷⑦봽 - 醫낅즺 ?쒓렇???湲?
    while (g_Running && server.IsRunning())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // English: Graceful shutdown
    // ?쒓?: ?뺤긽 醫낅즺
    Network::Utils::Logger::Info("Shutting down server...");
    server.Stop();
    g_pServer = nullptr;

    Network::Utils::Logger::Info("Server stopped.");
    std::cout << "Server shutdown complete." << std::endl;

    return 0;
}

