#pragma once

// 주기적/단발 콜백을 위한 단일 스레드 min-heap 타이머 큐.
//
// 설계:
//   - 단일 백그라운드 워커 스레드(std::thread).
//   - 콜백은 워커 스레드에서 실행; 짧게 유지하거나 풀로 오프로드할 것.
//   - ScheduleRepeat: 콜백이 true를 반환하면 재등록, false를 반환하면 자동 해제.
//   - Cancel(): 핸들을 취소 표시; 동시 호출 안전.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

namespace Network::Concurrency
{

class TimerQueue
{
  public:
	using TimerHandle = uint64_t;
	using TimerCallback = std::function<void()>;

	TimerQueue();
	~TimerQueue();

	TimerQueue(const TimerQueue &) = delete;
	TimerQueue &operator=(const TimerQueue &) = delete;

	// 백그라운드 워커 스레드를 시작한다.
	bool Initialize();

	// 워커 스레드를 정지하고 대기 중인 모든 항목을 폐기한다.
	void Shutdown();

	// delayMs 밀리초 후 콜백을 1회 실행한다.
	TimerHandle ScheduleOnce(TimerCallback cb, uint32_t delayMs);

	// intervalMs 마다 콜백을 반복 실행한다.
	// 콜백이 true를 반환하면 재등록, false를 반환하면 자동 해제된다.
	TimerHandle ScheduleRepeat(std::function<bool()> cb, uint32_t intervalMs);

	// 등록된 타이머를 취소한다. 이미 실행됐거나 없으면 no-op.
	// 실행 중인 콜백과 동시 호출해도 안전하다.
	bool Cancel(TimerHandle handle);

	bool IsRunning() const
	{
		return mRunning.load(std::memory_order_acquire);
	}

  private:
	struct TimerEntry
	{
		TimerHandle handle{0};                          // 고유 식별자; Cancel() 호출 시 조회 키
		std::chrono::steady_clock::time_point nextFire; // 다음 실행 예정 시각 (min-heap 정렬 기준)
		uint32_t intervalMs{0};                         // 반복 간격 ms; 0 = 단발(one-shot)
		std::function<bool()> cb;                       // 실행할 콜백; true 반환 시 재등록, false 시 해제
	};

	// min-heap 비교자: nextFire가 가장 이른 항목이 heap front에 위치한다.
	struct EntryCompare
	{
		bool operator()(const TimerEntry &a, const TimerEntry &b) const
		{
			return a.nextFire > b.nextFire;
		}
	};

	// 힙에 항목을 삽입하고 워커를 깨운다.
	void PushEntry(TimerEntry e);

	// 힙 최상단(가장 이른) 항목을 꺼낸다.
	// 호출자가 mMutex를 보유 중이어야 하며 힙이 비어있지 않아야 한다.
	TimerEntry PopTop();

	void WorkerLoop();

	// ─────────────────────────────────────────────
	// 힙 & 동기화
	// ─────────────────────────────────────────────
	// 벡터 기반 min-heap (std::push_heap / std::pop_heap 사용).
	std::vector<TimerEntry> mHeap;               // nextFire 기준 min-heap; mMutex 보호
	mutable std::mutex mMutex;                   // mHeap / mCancelledHandles 접근 직렬화
	std::condition_variable mCV;                 // 새 항목 추가·Cancel·Shutdown 시 notify → WorkerLoop 깨움

	// ─────────────────────────────────────────────
	// 워커 스레드 & 생명주기
	// ─────────────────────────────────────────────
	std::thread mWorkerThread;                   // 타이머 만료를 처리하는 단일 백그라운드 스레드
	std::atomic<bool> mRunning{false};           // true → 실행 중; false → Shutdown 신호 (acq_rel)
	std::atomic<TimerHandle> mNextHandle{1};     // 발급할 다음 핸들 값; fetch_add(relaxed)로 단조 증가

	// ─────────────────────────────────────────────
	// 취소 집합
	// ─────────────────────────────────────────────
	// pop과 실행 사이에 Cancel()로 취소된 핸들을 보관한다 (mMutex 보호).
	// 원샷 타이머가 실행된 뒤 Cancel()이 호출되어도 이 집합에서 즉시 제거되므로
	// 핸들이 무한히 누적되지 않는다.
	std::unordered_set<TimerHandle> mCancelledHandles;  // 실행 전 폐기할 핸들 집합
};

} // namespace Network::Concurrency
