#pragma once

// English: Buffer management utility
// 한글: 버퍼 관리 유틸리티

#include "NetworkTypes.h"
#include <memory>
#include <mutex>
#include <atomic>
#include <cstdint>

namespace Network::Utils
{
// =============================================================================
// English: BufferManager - manages memory buffers for network operations
// 한글: BufferManager - 네트워크 작업용 메모리 버퍼 관리
// =============================================================================

class BufferManager
{
public:
	// English: Constructor with default buffer size
	// 한글: 기본 버퍼 크기로 생성자
	BufferManager(size_t bufferSize = DEFAULT_BUFFER_SIZE)
		: mDefaultBufferSize(bufferSize)
		, mTotalAllocated(0)
		, mCurrentUsed(0)
		, mPeakUsed(0)
	{
	}

	~BufferManager() = default;

	// English: Allocate a new buffer with statistics tracking
	// 한글: 통계 추적을 포함한 새 버퍼 할당
	// @param size - Size of buffer to allocate (0 = use default size)
	// @return Unique pointer to allocated buffer
	std::unique_ptr<uint8_t[]> Allocate(size_t size)
	{
		std::lock_guard<std::mutex> lock(mMutex);
		
		// English: Update statistics
		// 한글: 통계 업데이트
		mTotalAllocated.fetch_add(1, std::memory_order_relaxed);
		size_t current = mCurrentUsed.fetch_add(1, std::memory_order_relaxed) + 1;
		
		// English: Update peak usage with lock-free CAS loop.
		//          Since mMutex is held, only this thread can modify mCurrentUsed,
		//          but mPeakUsed may be read by other threads without lock.
		//          CAS ensures peak update is atomic against concurrent readers.
		// 한글: 락프리 CAS 루프로 최대 사용량 업데이트.
		//       mMutex가 보유되었으므로 mCurrentUsed는 이 스레드만 수정하지만,
		//       mPeakUsed는 다른 스레드가 락 없이 읽을 수 있음.
		//       CAS는 동시 읽기 스레드에 대해 peak 업데이트의 원자성 보장.
		size_t peak = mPeakUsed.load(std::memory_order_relaxed);
		while (current > peak)
		{
			if (mPeakUsed.compare_exchange_weak(peak, current, std::memory_order_relaxed))
				break;
		}
		
		return std::make_unique<uint8_t[]>(size > 0 ? size : mDefaultBufferSize);
	}

	// English: Deallocate buffer and update statistics
	// 한글: 버퍼 할당 해제 및 통계 업데이트
	void Deallocate(uint8_t* /*buffer*/)
	{
		// English: Update current usage count
		// 한글: 현재 사용량 카운트 업데이트
		mCurrentUsed.fetch_sub(1, std::memory_order_relaxed);
	}

	// English: Get total number of buffers allocated (lifetime)
	// 한글: 총 할당된 버퍼 수 가져오기 (전체 수명)
	size_t GetPoolSize() const
	{
		return mTotalAllocated.load(std::memory_order_relaxed);
	}

	// English: Get number of currently used buffers
	// 한글: 현재 사용 중인 버퍼 수 가져오기
	size_t GetUsedBuffers() const
	{
		return mCurrentUsed.load(std::memory_order_relaxed);
	}

	// English: Get peak number of buffers used simultaneously
	// 한글: 동시에 사용된 버퍼의 최대 수 가져오기
	size_t GetPeakUsed() const
	{
		return mPeakUsed.load(std::memory_order_relaxed);
	}

	// English: Reset statistics
	// 한글: 통계 초기화
	void ResetStatistics()
	{
		std::lock_guard<std::mutex> lock(mMutex);
		mTotalAllocated.store(0, std::memory_order_relaxed);
		mCurrentUsed.store(0, std::memory_order_relaxed);
		mPeakUsed.store(0, std::memory_order_relaxed);
	}

private:
	size_t mDefaultBufferSize;
	
	// English: Statistics tracking
	// 한글: 통계 추적
	std::atomic<size_t> mTotalAllocated;  // Total allocations made
	std::atomic<size_t> mCurrentUsed;     // Currently in use
	std::atomic<size_t> mPeakUsed;        // Peak concurrent usage
	
	mutable std::mutex mMutex;
};

} // namespace Network::Utils
