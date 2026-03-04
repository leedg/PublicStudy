#pragma once
// English: Core/Memory/IBufferPool.h — Platform-agnostic buffer pool interface.
//          Each concrete pool manages a contiguous slab split into fixed-size slots.
// 한글: 플랫폼 독립적인 버퍼 풀 인터페이스.
//       각 구현체는 고정 크기 슬롯으로 분할된 연속 메모리 슬랩을 관리한다.

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
	void*  ptr      = nullptr; // English: pointer to slot memory (nullptr → pool exhausted)
	                           // 한글: 슬롯 메모리 포인터 (nullptr → 풀 소진)
	size_t index    = 0;       // English: slot index for Release()
	                           // 한글: Release() 호출용 슬롯 인덱스
	size_t capacity = 0;       // English: slot size in bytes
	                           // 한글: 슬롯 크기 (바이트)
};

class IBufferPool
{
public:
	virtual ~IBufferPool() = default;

	virtual bool Initialize(size_t poolSize, size_t slotSize) = 0;
	virtual void Shutdown() = 0;

	// English: Returns a slot; slot.ptr == nullptr means pool exhausted.
	// 한글: 슬롯 반환. slot.ptr == nullptr 이면 풀 소진.
	virtual BufferSlot Acquire()             = 0;
	virtual void       Release(size_t index) = 0;

	virtual size_t SlotSize()  const = 0;
	virtual size_t PoolSize()  const = 0;
	virtual size_t FreeCount() const = 0;
};

// English: Platform-specific helpers are provided as non-virtual concrete methods
//          in derived classes (RIOBufferPool, IOUringBufferPool) only.
//          Do NOT add platform #if virtual methods here.
// 한글: 플랫폼별 헬퍼는 파생 클래스에만 비가상 구체 메서드로 제공한다.
//       여기에 플랫폼 #if 가상 메서드 추가 금지.

} // namespace Memory
} // namespace Core
} // namespace Network
