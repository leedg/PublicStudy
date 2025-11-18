#include <iostream>
#include <thread>
#include <chrono>
#include <cstdint>
#include <vector>
#include "LockFreeSPSCQueue.h"

int main() {
    using namespace std::chrono;
    constexpr std::size_t N = 1'000'000; // number of items
    LockFreeSPSCQueue<uint64_t, 1024> queue; // SPSC queue with capacity 1024

    std::atomic<bool> start{false};

    uint64_t produced_sum = 0;
    uint64_t consumed_sum = 0;

    std::thread producer([&](){
        while (!start.load(std::memory_order_acquire)) {}
        for (uint64_t i = 1; i <= N; ++i) {
            while (!queue.push(i)) {
                // backoff: queue is full, yield briefly
                std::this_thread::yield();
            }
            produced_sum += i;
        }
    });

    std::thread consumer([&](){
        while (!start.load(std::memory_order_acquire)) {}
        uint64_t value;
        uint64_t count = 0;
        while (count < N) {
            if (queue.pop(value)) {
                consumed_sum += value;
                ++count;
            } else {
                std::this_thread::yield();
            }
        }
    });

    auto t0 = high_resolution_clock::now();
    start.store(true, std::memory_order_release);

    producer.join();
    consumer.join();
    auto t1 = high_resolution_clock::now();

    auto ms = duration_cast<milliseconds>(t1 - t0).count();

    std::cout << "Lock-Free SPSC Queue demo\n";
    std::cout << "Items transferred: " << N << "\n";
    std::cout << "Elapsed: " << ms << " ms\n";
    std::cout << "Producer sum: " << produced_sum << ", Consumer sum: " << consumed_sum << "\n";
    std::cout << (produced_sum == consumed_sum ? "OK: sums match" : "ERROR: sums differ") << "\n";

    return produced_sum == consumed_sum ? 0 : 1;
}
