#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <cstdint>
#include "LockFreeMPMCQueue.h"

int main() {
    using namespace std::chrono;

    constexpr std::size_t Capacity = 1 << 12; // 4096
    constexpr int Producers = 4;
    constexpr int Consumers = 4;
    constexpr std::uint64_t PerProducer = 250'000; // total ~ 1,000,000

    LockFreeMPMCQueue<std::uint64_t, Capacity> q;

    std::atomic<bool> start{false};
    std::atomic<std::uint64_t> produced_sum{0};
    std::atomic<std::uint64_t> consumed_sum{0};

    std::vector<std::thread> threads;
    threads.reserve(Producers + Consumers);

    for (int p = 0; p < Producers; ++p) {
        threads.emplace_back([&, p](){
            while (!start.load(std::memory_order_acquire)) {}
            for (std::uint64_t i = 1; i <= PerProducer; ++i) {
                std::uint64_t v = i + (std::uint64_t)p * PerProducer;
                while (!q.enqueue(v)) {
                    std::this_thread::yield();
                }
                produced_sum.fetch_add(v, std::memory_order_relaxed);
            }
        });
    }

    for (int c = 0; c < Consumers; ++c) {
        threads.emplace_back([&, c](){
            while (!start.load(std::memory_order_acquire)) {}
            std::uint64_t local = 0;
            std::uint64_t total = Producers * PerProducer;
            for (;;) {
                std::uint64_t v;
                if (q.dequeue(v)) {
                    consumed_sum.fetch_add(v, std::memory_order_relaxed);
                    ++local;
                    if (local > total) break; // safety
                } else {
                    if (produced_sum.load(std::memory_order_relaxed) != 0 &&
                        q.empty()) {
                        // Best-effort termination condition decided below
                    }
                    if (local >= total) break;
                    std::this_thread::yield();
                }
                // We'll exit after main thread detects all produced consumed
            }
        });
    }

    auto t0 = high_resolution_clock::now();
    start.store(true, std::memory_order_release);

    // Wait until all values produced are consumed
    const std::uint64_t expected_total = [&](){
        std::uint64_t sum = 0;
        for (int p = 0; p < Producers; ++p) {
            std::uint64_t base = (std::uint64_t)p * PerProducer;
            for (std::uint64_t i = 1; i <= PerProducer; ++i) sum += base + i;
        }
        return sum;
    }();

    // Join producers first, then drain for consumers
    for (int i = 0; i < Producers; ++i) threads[i].join();

    // Busy wait until queue drained
    while (consumed_sum.load(std::memory_order_relaxed) != expected_total) {
        std::this_thread::yield();
    }

    // Signal consumers to exit by letting loop condition finish
    for (int i = Producers; i < Producers + Consumers; ++i) {
        if (threads[i].joinable()) threads[i].detach();
    }

    auto t1 = high_resolution_clock::now();
    auto ms = duration_cast<milliseconds>(t1 - t0).count();

    std::cout << "Lock-Free MPMC Queue demo\n";
    std::cout << "Producers: " << Producers << ", Consumers: " << Consumers << "\n";
    std::cout << "Expected sum: " << expected_total << "\n";
    std::cout << "Produced sum: " << produced_sum.load() << "\n";
    std::cout << "Consumed sum: " << consumed_sum.load() << "\n";
    std::cout << (consumed_sum.load() == expected_total ? "OK: sums match" : "ERROR: sums differ") << "\n";
    std::cout << "Elapsed: " << ms << " ms\n";

    return consumed_sum.load() == expected_total ? 0 : 1;
}
