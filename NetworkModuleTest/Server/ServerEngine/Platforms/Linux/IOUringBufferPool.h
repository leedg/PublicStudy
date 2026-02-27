#pragma once
// English: Linux io_uring buffer pool — alias for AsyncBufferPool.
// 한글: Linux io_uring 버퍼 풀 — AsyncBufferPool의 플랫폼 별칭.

#include "Platforms/AsyncBufferPool.h"

#if defined(__linux__)
namespace Network
{
namespace AsyncIO
{
namespace Linux
{
    using IOUringBufferPool = ::Network::AsyncIO::AsyncBufferPool;
} // namespace Linux
} // namespace AsyncIO
} // namespace Network
#endif // defined(__linux__)
