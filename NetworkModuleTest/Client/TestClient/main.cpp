// English: TestClient entry point - connects to game server and runs ping/pong
// 한글: TestClient 진입점 - 게임 서버에 접속하여 핑/퐁 실행

#include "include/TestClient.h"
#include "Utils/NetworkUtils.h"
#include <iostream>
#include <string>
#include <csignal>
#include <thread>
#include <chrono>
#include <conio.h>

using namespace Network::Utils;
using namespace Network::TestClient;

// English: Global client pointer for signal handling
// 한글: 시그널 처리용 전역 클라이언트 포인터
static TestClient* g_pClient = nullptr;

void SignalHandler(int signum)
{
    Logger::Info("Signal received: " + std::to_string(signum));
    if (g_pClient)
    {
        g_pClient->RequestStop();
    }
}

void PrintUsage(const char* programName)
{
    std::cout << "Usage: " << programName << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --host <addr>   Server address (default: 127.0.0.1)" << std::endl;
    std::cout << "  --port <port>   Server port (default: 9000)" << std::endl;
    std::cout << "  -l <level>      Log level: DEBUG, INFO, WARN, ERROR (default: INFO)" << std::endl;
    std::cout << "  -h, --help      Show this help" << std::endl;
}

void PrintStats(const TestClient& client)
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
        std::cout << "  Avg RTT    : " << static_cast<int>(stats.avgRtt) << " ms" << std::endl;
    }
    else
    {
        std::cout << "  (no pong received yet)" << std::endl;
    }
    std::cout << "--------------------------" << std::endl;
    std::cout << std::endl;
}

int main(int argc, char* argv[])
{
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
            if (level == "DEBUG") logLevel = LogLevel::Debug;
            else if (level == "WARN") logLevel = LogLevel::Warn;
            else if (level == "ERROR") logLevel = LogLevel::Err;
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
        Logger::Error("Failed to initialize Winsock");
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
    std::cout << "Connected! Press 'q' to quit, 's' for statistics." << std::endl;
    std::cout << std::endl;

    // English: Main loop - user input handling
    // 한글: 메인 루프 - 사용자 입력 처리
    while (!client.IsStopRequested() && client.IsConnected())
    {
        if (_kbhit())
        {
            char ch = static_cast<char>(_getch());
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

    Logger::Info("TestClient shutdown complete.");
    return 0;
}
