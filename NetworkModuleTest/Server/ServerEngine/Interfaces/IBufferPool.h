#pragma once
// English: Abstract buffer pool interface for pre-registered async I/O buffers.
// 한글: 사전 등록 비동기 I/O 버퍼 풀 추상 인터페이스.
//       RIOBufferPool, IOUringBufferPool 등이 구현한다.
//       나중에 멀티 사이즈 풀이나 Lock-Free 풀로 교체 시 이 인터페이스만 유지하면 된다.

#include <cstddef>
#include <cstdint>

namespace Network
{
namespace AsyncIO
{

class AsyncIOProvider;

class IBufferPool
{
  public:
    virtual ~IBufferPool() = default;

    // English: Initialize pool - allocate and pre-register bufferSize * poolSize bytes with provider.
    // 한글: 초기화 - provider에 bufferSize 크기 버퍼 poolSize개 사전 등록.
    virtual bool Initialize(AsyncIOProvider *provider, size_t bufferSize,
                            size_t poolSize) = 0;

    // English: Release all registered buffers and free memory.
    // 한글: 등록된 모든 버퍼를 해제하고 메모리를 반환한다.
    virtual void Shutdown() = 0;

    // English: Acquire a free buffer. Returns nullptr if pool is exhausted.
    // 한글: 빈 버퍼를 반환한다. 풀이 고갈된 경우 nullptr 반환.
    virtual uint8_t *Acquire(int64_t &outBufferId) = 0;

    // English: Return a buffer back to the pool.
    // 한글: 버퍼를 풀로 반환한다.
    virtual void Release(int64_t bufferId) = 0;

    virtual size_t GetBufferSize() const = 0;
    virtual size_t GetAvailable() const = 0;
    virtual size_t GetPoolSize() const = 0;
};

} // namespace AsyncIO
} // namespace Network
