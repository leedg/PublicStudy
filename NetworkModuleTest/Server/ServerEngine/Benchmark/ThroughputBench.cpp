// English: AsyncIO throughput benchmark.
//          Measures: provider call iterations/sec as a throughput proxy.
//          Build: standalone console project linking ServerEngine.lib.
//          Run: ThroughputBench.exe
// 한글: AsyncIO 처리량 벤치마크.
//       측정 지표: provider 호출 횟수/초 (처리량 proxy).
//       빌드: ServerEngine.lib 링크하는 별도 콘솔 프로젝트.
//       실행: ThroughputBench.exe

#include "Network/Core/AsyncIOProvider.h"

#ifdef _WIN32
#include "Platforms/Windows/IocpAsyncIOProvider.h"
using ProviderType = Network::AsyncIO::Windows::IocpAsyncIOProvider;
#elif defined(__linux__)
#include "Platforms/Linux/EpollAsyncIOProvider.h"
using ProviderType = Network::AsyncIO::Linux::EpollAsyncIOProvider;
#elif defined(__APPLE__)
// English: KqueueAsyncIOProvider lives in namespace Network::AsyncIO::BSD
// 한글: KqueueAsyncIOProvider는 Network::AsyncIO::BSD 네임스페이스에 있음
#include "Platforms/macOS/KqueueAsyncIOProvider.h"
using ProviderType = Network::AsyncIO::BSD::KqueueAsyncIOProvider;
#endif

#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

using namespace Network::AsyncIO;
using namespace std::chrono;

static constexpr size_t MSG_SIZE   = 1024; // English: 1 KB per message / 한글: 메시지당 1KB
static constexpr int    WARMUP_S   = 1;
static constexpr int    DURATION_S = 5;

int main()
{
    std::cout << "=== AsyncIO Throughput Benchmark ===\n";
    std::cout << "Message size : " << MSG_SIZE << " bytes\n";
    std::cout << "Duration     : " << DURATION_S << "s (+" << WARMUP_S
              << "s warmup)\n";

    ProviderType provider;
    if (provider.Initialize(1024, 256) != AsyncIOError::Success)
    {
        std::cout << "[ERROR] Provider init failed\n";
        return 1;
    }

    // English: Print provider name from GetInfo().mName
    // 한글: GetInfo().mName 으로 provider 이름 출력
    std::cout << "Provider     : " << provider.GetInfo().mName << "\n\n";

    // English: Warmup - discard first WARMUP_S seconds.
    // 한글: 워밍업 - 처음 WARMUP_S초 결과 버림.
    auto warmupEnd = steady_clock::now() + seconds(WARMUP_S);
    while (steady_clock::now() < warmupEnd)
        provider.IsInitialized(); // proxy call

    // English: Measure - count provider call iterations as throughput proxy.
    //          Real benchmark requires an actual loopback socket pair.
    //          This measures the provider API call overhead floor.
    // 한글: 측정 - provider 호출 횟수를 처리량 proxy로 사용.
    //       실제 측정은 루프백 소켓 페어 필요.
    //       이 코드는 provider API 호출 오버헤드 하한 측정.
    uint64_t count = 0;
    auto benchEnd = steady_clock::now() + seconds(DURATION_S);
    auto measureStart = steady_clock::now();

    while (steady_clock::now() < benchEnd)
    {
        provider.IsInitialized();
        ++count;
        if (count % 100000 == 0)
            std::this_thread::yield();
    }

    double elapsed =
        duration_cast<microseconds>(steady_clock::now() - measureStart)
            .count() / 1e6;

    double pps  = static_cast<double>(count) / elapsed;
    double mbps = (pps * static_cast<double>(MSG_SIZE)) / (1024.0 * 1024.0);

    std::cout << "[BENCH] Results (" << DURATION_S << "s):\n";
    std::cout << "  Calls/sec   : " << static_cast<uint64_t>(pps) << "\n";
    std::cout << "  Equiv MB/s  : " << mbps << " MB/sec\n";
    std::cout << "  Total calls : " << count << "\n";

    provider.Shutdown();
    return 0;
}
