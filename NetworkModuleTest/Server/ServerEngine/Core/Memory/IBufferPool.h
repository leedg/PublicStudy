#pragma once
// Core/Memory/IBufferPool.h — 플랫폼 독립적인 버퍼 풀 인터페이스.
// 각 구현체는 고정 크기 슬롯으로 분할된 연속 메모리 슬랩을 관리한다.
//
// 구현 계층:
//   - StandardBufferPool : POSIX posix_memalign / Windows _aligned_malloc 기반, 등록 없음.
//   - RIOBufferPool       : VirtualAlloc + 1회 RIORegisterBuffer (Windows 8+), per-op pin 비용 없음.
//   - IOUringBufferPool   : posix_memalign + io_uring_register_buffers (Linux), zero-copy 고정 버퍼.
//   - RIOBufferPool / IOUringBufferPool 은 각 플랫폼 헤더에서 AsyncBufferPool using alias로도 제공.

#include <cstddef>
#include <cstdint>

namespace Network
{
namespace Core
{
namespace Memory
{

struct BufferSlot
{
	void*  ptr      = nullptr; // 슬롯 메모리 포인터 (nullptr → 풀 소진)
	size_t index    = 0;       // Release() 호출용 슬롯 인덱스
	size_t capacity = 0;       // 슬롯 크기 (바이트)
};

class IBufferPool
{
public:
	virtual ~IBufferPool() = default;

	virtual bool Initialize(size_t poolSize, size_t slotSize) = 0;
	virtual void Shutdown() = 0;

	// 슬롯 반환. slot.ptr == nullptr 이면 풀 소진.
	virtual BufferSlot Acquire()             = 0;
	virtual void       Release(size_t index) = 0;

	virtual size_t SlotSize()  const = 0;
	virtual size_t PoolSize()  const = 0;
	virtual size_t FreeCount() const = 0;
};

// 플랫폼별 헬퍼(RIO 버퍼 ID 조회, io_uring 고정 버퍼 인덱스 등)는
// 파생 클래스(RIOBufferPool, IOUringBufferPool)에만 비가상 구체 메서드로 제공한다.
// IBufferPool에 플랫폼 #if 가상 메서드 추가 금지.

} // namespace Memory
} // namespace Core
} // namespace Network
