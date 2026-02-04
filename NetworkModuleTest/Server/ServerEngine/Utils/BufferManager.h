#pragma once

// English: Buffer management utility
// 한글: 버퍼 관리 유틸리티

#include "NetworkTypes.h"
#include <memory>
#include <mutex>
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
	{
	}

	~BufferManager() = default;

	// English: Allocate a new buffer
	// 한글: 새 버퍼 할당
	// @param size - Size of buffer to allocate (0 = use default size)
	// @return Unique pointer to allocated buffer
	std::unique_ptr<uint8_t[]> Allocate(size_t size)
	{
		std::lock_guard<std::mutex> lock(mMutex);
		return std::make_unique<uint8_t[]>(size > 0 ? size : mDefaultBufferSize);
	}

	// English: Deallocate buffer (no-op for unique_ptr)
	// 한글: 버퍼 할당 해제 (unique_ptr는 자동 처리)
	void Deallocate(uint8_t* /*buffer*/)
	{
		// English: No-op for unique_ptr-based allocation
		// 한글: unique_ptr 기반 할당에서는 자동으로 처리됨
	}

	// English: Get pool size (not implemented for simple allocation)
	// 한글: 풀 크기 가져오기 (단순 할당 방식에서는 미구현)
	size_t GetPoolSize() const
	{
		return 0; // Not implemented
	}

	// English: Get number of used buffers (not implemented)
	// 한글: 사용 중인 버퍼 수 가져오기 (미구현)
	size_t GetUsedBuffers() const
	{
		return 0; // Not implemented
	}

private:
	size_t mDefaultBufferSize;
	mutable std::mutex mMutex;
};

} // namespace Network::Utils
