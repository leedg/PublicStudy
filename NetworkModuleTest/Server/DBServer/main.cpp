// TestDBServer entry point - initializes and runs the database server

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

// Global server instance for signal handling
static Network::DBServer::TestDBServer* g_pServer = nullptr;
static std::atomic<bool> g_Running{ true };
static std::atomic<bool> g_ShutdownComplete{ false };

// Signal handler for graceful shutdown
void SignalHandler(int signum)
{
    Network::Utils::Logger::Info("Signal received: " + std::to_string(signum));
    g_Running = false;
}

#ifdef _WIN32
// Console ctrl handler — catches CTRL_CLOSE_EVENT / taskkill so server.Stop() runs.
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
        // Wait up to 8s for main thread to finish server.Stop()
        for (int i = 0; i < 80 && !g_ShutdownComplete.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return TRUE;
    default:
        return FALSE;
    }
}
#endif

// Print usage information
void PrintUsage(const char* programName)
{
    std::cout << "Usage: " << programName << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -p <port>       Server port (default: " << Network::Utils::DEFAULT_TEST_DB_PORT << ")" << std::endl;
    std::cout << "  -w <count>      DB worker threads (default: " << Network::Utils::DEFAULT_DB_WORKER_COUNT << ")" << std::endl;
    std::cout << "  -l <level>      Log level: DEBUG, INFO, WARN, ERROR (default: INFO)" << std::endl;
    std::cout << "  -h              Show this help" << std::endl;
}

// Parse log level string
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
    // Set console code page to UTF-8 for Korean output
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);

    // Install crash dump handler — writes .dmp + .crash on unhandled exception
    Network::Utils::CrashDump::Initialize("./dumps/");
#endif

    std::cout << "====================================" << std::endl;
    std::cout << "  TestDBServer - Database Server" << std::endl;
    std::cout << "====================================" << std::endl;

    // Default settings
    uint16_t port          = Network::Utils::DEFAULT_TEST_DB_PORT;
    size_t   dbWorkerCount = Network::Utils::DEFAULT_DB_WORKER_COUNT;
    Network::Utils::LogLevel logLevel = Network::Utils::LogLevel::Info;

    // Parse command line arguments
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
        else if (arg == "-w" && i + 1 < argc)
        {
            dbWorkerCount = static_cast<size_t>(std::stoi(argv[++i]));
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

    // Setup logging
    Network::Utils::Logger::SetLevel(logLevel);

    // Register signal handlers
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
#ifdef _WIN32
    // SIGBREAK is Windows-specific
    #ifdef SIGBREAK
    std::signal(SIGBREAK, SignalHandler);
    #endif
    // Catch CTRL_CLOSE_EVENT for graceful shutdown on window close / taskkill
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
#endif

    // Create and initialize server
    Network::DBServer::TestDBServer server;
    g_pServer = &server;

    Network::Utils::Logger::Info("Initializing TestDBServer on port " + std::to_string(port));

    if (!server.Initialize(port, dbWorkerCount))
    {
        Network::Utils::Logger::Error("Failed to initialize server");
        return 1;
    }

    // Start server
    if (!server.Start())
    {
        Network::Utils::Logger::Error("Failed to start server");
        return 1;
    }

    Network::Utils::Logger::Info("TestDBServer is running. Press Ctrl+C to stop.");

    // Main loop — waits for SIGINT/SIGTERM, ConsoleCtrlHandler, or Named Event.
    //          Named event "TestDBServer_GracefulShutdown" allows test scripts to trigger
    //          graceful shutdown without console manipulation (no TerminateProcess).
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

    // Graceful shutdown
    Network::Utils::Logger::Info("Shutting down TestDBServer...");
    server.Stop();
    g_pServer = nullptr;

    // Signal ConsoleCtrlHandler (if waiting) that cleanup is done
    g_ShutdownComplete.store(true);

    Network::Utils::Logger::Info("TestDBServer stopped.");
    std::cout << "Server shutdown complete." << std::endl;

    return 0;
}
