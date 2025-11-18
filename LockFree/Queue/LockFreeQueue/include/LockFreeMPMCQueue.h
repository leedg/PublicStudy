#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <new>
#include <utility>

// 한국어 설명
// - 본 구현은 Dmitry Vyukov가 제안한 유한(고정 크기) MPMC(다중 생산자/다중 소비자) 락프리 큐를 기반으로 합니다.
// - 각 셀(Cell)은 순서값(seq)을 갖고, 생산/소비 진행 위치(pos)와의 차이를 통해 비었는지/가득 찼는지 판정합니다.
// - 메모리 순서는 acquire/release를 사용해 생산자가 쓴 데이터를 소비자가 올바르게 볼 수 있도록, 반대로 소비가 완료된 공간을
//   생산자가 다시 사용할 수 있도록 보장합니다. CAS는 위치 인수(pos)를 선점하기 위한 용도로만 사용합니다.
// - 용량(Capacity)은 2의 거듭제곱이어야 하며, 이는 인덱스 순환 시 모듈러 연산을 비트 마스크로 대체하기 위함입니다.

// Bounded MPMC lock-free queue based on Dmitry Vyukov's algorithm
// Capacity must be a power of two

template <typename T, std::size_t Capacity>
class LockFreeMPMCQueue {
    // Capacity가 2의 거듭제곱인지 컴파일 타임에 강제
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    LockFreeMPMCQueue() noexcept {
        // 각 셀의 seq를 자신의 인덱스로 초기화
        //  - enqueue 시: seq == pos 이면 해당 pos에 넣을 수 있음
        //  - dequeue 시: seq == pos + 1 이면 해당 pos에서 꺼낼 수 있음
        for (std::size_t i = 0; i < Capacity; ++i) {
            _cells[i].seq.store(i, std::memory_order_relaxed);
        }
        // 생산/소비 전역 위치 초기화
        _enqueuePos.store(0, std::memory_order_relaxed);
        _dequeuePos.store(0, std::memory_order_relaxed);
    }

    ~LockFreeMPMCQueue() {
        // 남아 있을 수 있는 구성 완료 객체 파기 시도(최선)
        T tmp;
        while (dequeue(tmp)) {}
    }

    // 복사 삽입: 내부적으로 perfect-forwarding emplace 사용
    bool enqueue(const T& data) noexcept { return emplace(data); }

    template <class... Args>
    bool emplace(Args&&... args) noexcept {
        Cell* cell;
        // 후보 enqueue 위치를 읽되, 강한 순서는 불필요하여 relaxed 사용
        std::size_t pos = _enqueuePos.load(std::memory_order_relaxed);
        for (;;) {
            // 해당 위치의 셀 확인
            cell = &_cells[pos & mask()];
            // 셀의 seq를 acquire로 읽어 이전 생산자가 저장한 데이터 가시성 확보
            std::size_t seq = cell->seq.load(std::memory_order_acquire);
            // seq - pos 비교로 상태 판별
            //  dif == 0  => 빈 셀(이 pos에 생산 가능)
            //  dif  < 0  => 아직 소비되지 않음(가득 참)
            //  dif  > 0  => 경쟁으로 pos가 진전됨(새 pos로 재시도)
            intptr_t dif = (intptr_t)seq - (intptr_t)pos;
            if (dif == 0) {
                // 이 위치를 선점하기 위해 전역 enqueuePos를 CAS로 1 증가 시도
                if (_enqueuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                    break; // 선점 성공
            } else if (dif < 0) {
                return false; // full: 큐가 가득 참
            } else {
                // 경쟁으로 pos가 앞서 있으므로 최신 pos로 재시도
                pos = _enqueuePos.load(std::memory_order_relaxed);
            }
        }
        // 셀의 storage 영역에 객체를 배치 생성(복사/이동/생성자 호출)
        new (&cell->storage) T(std::forward<Args>(args)...);
        // seq에 pos+1을 기록하여 '데이터 준비 완료'를 release로 공개
        cell->seq.store(pos + 1, std::memory_order_release);
        return true;
    }

    bool dequeue(T& data) noexcept {
        Cell* cell;
        // 후보 dequeue 위치를 읽음(relaxed)
        std::size_t pos = _dequeuePos.load(std::memory_order_relaxed);
        for (;;) {
            // 해당 위치의 셀 확인
            cell = &_cells[pos & mask()];
            // seq를 acquire로 읽어 생산자가 release로 공개한 쓰기 가시성 확보
            std::size_t seq = cell->seq.load(std::memory_order_acquire);
            // seq - (pos+1) 비교로 상태 판별
            //  dif == 0  => 채워진 셀(이 pos에서 소비 가능)
            //  dif  < 0  => 비어 있음
            //  dif  > 0  => 경쟁으로 pos가 진전됨(새 pos로 재시도)
            intptr_t dif = (intptr_t)seq - (intptr_t)(pos + 1);
            if (dif == 0) {
                // 이 위치를 선점하기 위해 전역 dequeuePos CAS로 1 증가 시도
                if (_dequeuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                    break; // 선점 성공
            } else if (dif < 0) {
                return false; // empty: 큐가 비어 있음
            } else {
                // 경쟁으로 pos가 앞서 있으므로 최신 pos로 재시도
                pos = _dequeuePos.load(std::memory_order_relaxed);
            }
        }
        // 저장된 객체를 꺼내 이동(copy/move)
        T* elem = reinterpret_cast<T*>(&cell->storage);
        data = std::move(*elem);
        // 소멸자 호출로 파기
        elem->~T();
        // seq에 pos + mask() + 1을 기록하여 '소비 완료, 빈 셀' 상태로 전환(release)
        //  (pos에 Capacity를 더한 다음 순서가 오기까지 기다리게 만드는 효과)
        cell->seq.store(pos + mask() + 1, std::memory_order_release);
        return true;
    }

    bool empty() const noexcept {
        // size는 근사치이므로 empty 역시 근사 결과임
        return size() == 0;
    }

    bool full() const noexcept {
        // size는 근사치이므로 full 역시 근사 결과임(경쟁 상황에서 오차 가능)
        return size() == Capacity;
    }

    std::size_t size() const noexcept {
        // 근사치 크기(원자값 두 개를 독립적으로 읽어 계산)
        std::size_t enq = _enqueuePos.load(std::memory_order_acquire);
        std::size_t deq = _dequeuePos.load(std::memory_order_acquire);
        return enq - deq;
    }

private:
    // 모듈러 연산 대체를 위한 마스크 (Capacity가 2의 거듭제곱일 때만 유효)
    static constexpr std::size_t mask() noexcept { return Capacity - 1; }

    struct Cell {
        // 각 셀의 상태를 나타내는 순서값
        //  - enqueue는 seq == pos일 때만 이 셀에 쓸 수 있음
        //  - dequeue는 seq == pos+1일 때만 이 셀에서 읽을 수 있음
        std::atomic<std::size_t> seq;
        // T를 디폴트 생성하지 않고 저장 공간만 확보(placement new 사용)
        // 정렬을 맞추기 위해 alignas(alignof(T)) 적용
        alignas(alignof(T)) unsigned char storage[sizeof(T)];
    };

    // 전역 생산/소비 위치. false sharing 방지를 위해 64바이트 정렬
    alignas(64) std::atomic<std::size_t> _enqueuePos{0};
    alignas(64) std::atomic<std::size_t> _dequeuePos{0};
    // 고정 길이 셀 배열(각 셀은 seq와 storage를 가짐)
    alignas(64) Cell _cells[Capacity];
};
