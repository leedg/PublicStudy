#pragma once

// 키 친화도 기반 순서 보장 비동기 디스패처.

#include "ExecutionQueue.h"
#include "Utils/Logger.h"
#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Network::Concurrency
{
// =============================================================================
// KeyedDispatcher
//
// 동일 key는 항상 동일 worker 큐로 라우팅된다 (key % workerCount 해시).
// 각 worker 큐는 단일 스레드가 처리하므로, 같은 key의 작업은
// 제출 순서대로 실행되는 per-key FIFO 순서 보장이 성립한다.
//
// 세션 ID를 key로 사용하면 동일 세션의 DB 작업이 항상 같은 worker에
// 직렬화되어, 별도의 락 없이 세션 단위 순서 보장을 얻을 수 있다.
// =============================================================================

class KeyedDispatcher
{
  public:
	struct Options
	{
		// 0 이면 std::thread::hardware_concurrency() 값을 사용;
		// hardware_concurrency()도 0을 반환하면 4로 폴백.
		size_t mWorkerCount = 0;                                       // 생성할 워커 스레드 수
		ExecutionQueueOptions<std::function<void()>> mQueueOptions;   // 각 워커 큐의 백프레셔/용량 설정
		std::string mName = "KeyedDispatcher";                         // 로그 식별용 이름
	};

	struct StatsSnapshot
	{
		size_t mSubmitted = 0;  // 누적 성공 제출 수
		size_t mRejected  = 0;  // 누적 거부(큐 포화 / 미실행) 수
		size_t mCompleted = 0;  // 누적 완료(예외 없음) 수
		size_t mFailed    = 0;  // 누적 실패(예외 발생) 수
	};

	KeyedDispatcher()
		: mRunning(false),
		  mSubmitted(0),
		  mRejected(0),
		  mCompleted(0),
		  mFailed(0)
	{
	}

	~KeyedDispatcher()
	{
		Shutdown();
	}

	KeyedDispatcher(const KeyedDispatcher &) = delete;
	KeyedDispatcher &operator=(const KeyedDispatcher &) = delete;

	bool Initialize(const Options &options)
	{
		if (mRunning.load(std::memory_order_acquire))
		{
			Utils::Logger::Warn(options.mName + ": already running");
			return true;
		}

		Options resolvedOptions = options;
		if (resolvedOptions.mWorkerCount == 0)
		{
			resolvedOptions.mWorkerCount = std::thread::hardware_concurrency();
			if (resolvedOptions.mWorkerCount == 0)
			{
				resolvedOptions.mWorkerCount = 4;
			}
		}

		mName = resolvedOptions.mName;
		mWorkers.clear();
		mWorkers.reserve(resolvedOptions.mWorkerCount);
		for (size_t i = 0; i < resolvedOptions.mWorkerCount; ++i)
		{
			mWorkers.push_back(std::make_unique<Worker>(resolvedOptions.mQueueOptions));
		}

		mRunning.store(true, std::memory_order_release);
		for (size_t i = 0; i < mWorkers.size(); ++i)
		{
			mWorkers[i]->mThread =
				std::thread(&KeyedDispatcher::WorkerThreadFunc, this, i);
		}

		Utils::Logger::Info(mName + ": initialized with " +
							std::to_string(mWorkers.size()) + " workers");
		return true;
	}

	void Shutdown()
	{
		bool expected = true;
		if (!mRunning.compare_exchange_strong(
				expected, false, std::memory_order_acq_rel))
		{
			return;
		}

		for (auto &worker : mWorkers)
		{
			worker->mQueue.Shutdown();
		}

		for (auto &worker : mWorkers)
		{
			if (worker->mThread.joinable())
			{
				worker->mThread.join();
			}
		}

		Utils::Logger::Info(mName + ": shutdown complete - submitted=" +
							std::to_string(mSubmitted.load(std::memory_order_relaxed)) +
							", completed=" +
							std::to_string(mCompleted.load(std::memory_order_relaxed)) +
							", failed=" +
							std::to_string(mFailed.load(std::memory_order_relaxed)) +
							", rejected=" +
							std::to_string(mRejected.load(std::memory_order_relaxed)));

		{
			// exclusive lock: clear 진행 중 Dispatch()가 mWorkers에 접근하지 못하도록 막음.
			// Dispatch()는 shared lock을 사용하므로 exclusive lock 획득 시 대기한다.
			std::unique_lock<std::shared_mutex> exclusiveLock(mWorkersMutex);
			mWorkers.clear();
		}
	}

	bool Dispatch(uint64_t key, std::function<void()> task, int timeoutMs = -1)
	{
		// shared lock: 다수의 Dispatch() 호출이 동시에 진행될 수 있도록 허용하면서,
		// Shutdown()의 mWorkers.clear()와의 TOCTOU 경쟁을 방지한다.
		// mRunning 확인과 mWorkers 접근이 동일 lock 범위 안에 있으므로
		// 확인 후 clear가 끼어드는 타이밍 문제가 발생하지 않는다.
		std::shared_lock<std::shared_mutex> sharedLock(mWorkersMutex);

		if (!mRunning.load(std::memory_order_acquire) || !task || mWorkers.empty())
		{
			mRejected.fetch_add(1, std::memory_order_relaxed);
			return false;
		}

		const size_t workerIndex = KeyToWorkerIndex(key);

		bool queued = mWorkers[workerIndex]->mQueue.Push(std::move(task), timeoutMs);
		if (queued)
		{
			mSubmitted.fetch_add(1, std::memory_order_relaxed);
			return true;
		}

		mRejected.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	bool IsRunning() const
	{
		return mRunning.load(std::memory_order_acquire);
	}

	size_t GetWorkerCount() const
	{
		std::shared_lock<std::shared_mutex> sharedLock(mWorkersMutex);
		return mWorkers.size();
	}

	size_t GetWorkerQueueSize(size_t workerIndex) const
	{
		std::shared_lock<std::shared_mutex> sharedLock(mWorkersMutex);
		if (workerIndex >= mWorkers.size())
		{
			return 0;
		}
		return mWorkers[workerIndex]->mQueue.Size();
	}

	StatsSnapshot GetStats() const
	{
		StatsSnapshot snapshot;
		snapshot.mSubmitted = mSubmitted.load(std::memory_order_relaxed);
		snapshot.mRejected = mRejected.load(std::memory_order_relaxed);
		snapshot.mCompleted = mCompleted.load(std::memory_order_relaxed);
		snapshot.mFailed = mFailed.load(std::memory_order_relaxed);
		return snapshot;
	}

  private:
	struct Worker
	{
		explicit Worker(
			const ExecutionQueueOptions<std::function<void()>> &queueOptions)
			: mQueue(queueOptions)
		{
		}

		ExecutionQueue<std::function<void()>> mQueue;  // 이 워커 전용 태스크 큐 (단일 스레드 소비)
		std::thread mThread;                           // 이 워커를 구동하는 백그라운드 스레드
	};

	size_t KeyToWorkerIndex(uint64_t key) const
	{
		// key % workerCount 해시: 동일 key는 항상 동일 인덱스로 수렴.
		// workerCount가 소수일 필요는 없으나, 2의 거듭제곱을 피하면
		// 편향된 key 분포(짝수 세션 ID 등)에서 부하 편중을 줄일 수 있다.
		return static_cast<size_t>(key % mWorkers.size());
	}

	void WorkerThreadFunc(size_t workerIndex)
	{
		// Raw 참조 안전성: mWorkers는 Shutdown()에서 exclusive lock 하에
		// 모든 워커 스레드를 join한 뒤에만 clear된다.
		// join이 clear()보다 먼저 완료되므로, 이 함수의 생명주기 동안
		// worker 참조는 항상 유효하다.
		Worker &worker = *mWorkers[workerIndex];
		std::function<void()> task;

		for (;;)
		{
			if (!worker.mQueue.Pop(task, 100))
			{
				if (!mRunning.load(std::memory_order_acquire) &&
					worker.mQueue.Empty())
				{
					break;
				}
				continue;
			}

			try
			{
				if (task)
				{
					task();
				}
				mCompleted.fetch_add(1, std::memory_order_relaxed);
			}
			catch (const std::exception &e)
			{
				mFailed.fetch_add(1, std::memory_order_relaxed);
				Utils::Logger::Error(mName + ": worker[" +
									 std::to_string(workerIndex) +
									 "] task exception: " + std::string(e.what()));
			}
			catch (...)
			{
				mFailed.fetch_add(1, std::memory_order_relaxed);
				Utils::Logger::Error(mName + ": worker[" +
									 std::to_string(workerIndex) +
									 "] unknown task exception");
			}
		}
	}

	// ─────────────────────────────────────────────
	// 식별 & 생명주기
	// ─────────────────────────────────────────────
	std::string mName;                                // 로그 출력에 사용되는 인스턴스 식별자
	std::atomic<bool> mRunning;                       // true → 실행 중; acq_rel로 Dispatch/Shutdown 간 가시성 보장

	// ─────────────────────────────────────────────
	// 워커 풀 & 동기화
	// ─────────────────────────────────────────────
	std::vector<std::unique_ptr<Worker>> mWorkers;    // 워커 인스턴스 배열 (mWorkersMutex 보호)
	// Dispatch()는 shared lock(다중 진입 허용), Shutdown()은 exclusive lock(단독 진입)을 사용.
	mutable std::shared_mutex mWorkersMutex;          // Dispatch(shared) / Shutdown(exclusive) 잠금 쌍

	// ─────────────────────────────────────────────
	// 통계 카운터 (relaxed: 정확한 순서 불필요, 최종 집계 목적)
	// ─────────────────────────────────────────────
	std::atomic<size_t> mSubmitted;  // 큐 등록 성공 누계
	std::atomic<size_t> mRejected;   // 큐 등록 실패(포화/미실행) 누계
	std::atomic<size_t> mCompleted;  // 태스크 정상 완료 누계
	std::atomic<size_t> mFailed;     // 태스크 예외 발생 누계
};

} // namespace Network::Concurrency
