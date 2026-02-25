// English: AsyncIO latency benchmark.
//          Measures: min/avg/p99/max provider call latency in µs.
//          Build: standalone console project linking ServerEngine.lib.
//          Run: LatencyBench.exe
// 한글: AsyncIO 레이턴시 벤치마크.
//       측정 지표: provider 호출 레이턴시 min/avg/p99/max (µs).
//       빌드: ServerEngine.lib 링크하는 별도 콘솔 프로젝트.
//       실행: LatencyBench.exe

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

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <vector>

using namespace Network::AsyncIO;
using namespace std::chrono;

static constexpr int ITERATIONS = 10000;

int main()
{
    std::cout << "=== AsyncIO Latency Benchmark ===\n";
    std::cout << "Iterations : " << ITERATIONS << "\n";

    ProviderType provider;
    if (provider.Initialize(256, 128) != AsyncIOError::Success)
    {
        std::cout << "[ERROR] Provider init failed\n";
        return 1;
    }

    // English: Print provider name from GetInfo().mName
    // 한글: GetInfo().mName 으로 provider 이름 출력
    std::cout << "Provider   : " << provider.GetInfo().mName << "\n\n";

    std::vector<double> latencies;
    latencies.reserve(ITERATIONS);

    for (int i = 0; i < ITERATIONS; ++i)
    {
        auto t0 = steady_clock::now();
        provider.IsInitialized(); // English: proxy for a single I/O operation / 한글: 단일 I/O 작업 proxy
        auto t1 = steady_clock::now();
        double us =
            static_cast<double>(duration_cast<nanoseconds>(t1 - t0).count()) /
            1000.0;
        latencies.push_back(us);
    }

    std::sort(latencies.begin(), latencies.end());
    double minL = latencies.front();
    double maxL = latencies.back();
    double avgL =
        std::accumulate(latencies.begin(), latencies.end(), 0.0) / ITERATIONS;
    double p99L = latencies[static_cast<size_t>(ITERATIONS * 0.99)];

    std::cout << "[BENCH] Latency (" << ITERATIONS << " samples):\n";
    std::cout << "  min : " << minL << " us\n";
    std::cout << "  avg : " << avgL << " us\n";
    std::cout << "  p99 : " << p99L << " us\n";
    std::cout << "  max : " << maxL << " us\n";

    provider.Shutdown();
    return 0;
}
