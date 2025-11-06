#pragma once
#include <atomic>
#include <array>
#include <cstddef>
#include <type_traits>

// Single-producer, single-consumer lock-free ring buffer queue
// Capacity must be a power of two for fast mask operations.
template <typename T, std::size_t Capacity>
class LockFreeSPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    LockFreeSPSCQueue() : _head(0), _tail(0) {}

    bool push(const T& value) noexcept {
        const auto tail = _tail.load(std::memory_order_relaxed);
        const auto head = _head.load(std::memory_order_acquire);
        if ((tail - head) == Capacity) {
            return false; // full
        }
        _buffer[tail & mask()] = value;
        _tail.store(tail + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& out) noexcept {
        const auto head = _head.load(std::memory_order_relaxed);
        const auto tail = _tail.load(std::memory_order_acquire);
        if (head == tail) {
            return false; // empty
        }
        out = _buffer[head & mask()];
        _head.store(head + 1, std::memory_order_release);
        return true;
    }

    bool empty() const noexcept { return _head.load(std::memory_order_acquire) == _tail.load(std::memory_order_acquire); }
    bool full() const noexcept { return (_tail.load(std::memory_order_acquire) - _head.load(std::memory_order_acquire)) == Capacity; }
    std::size_t size() const noexcept { return _tail.load(std::memory_order_acquire) - _head.load(std::memory_order_acquire); }

private:
    static constexpr std::size_t mask() noexcept { return Capacity - 1; }

    alignas(64) std::atomic<std::size_t> _head;
    alignas(64) std::atomic<std::size_t> _tail;
    alignas(64) std::array<T, Capacity> _buffer{};
};
