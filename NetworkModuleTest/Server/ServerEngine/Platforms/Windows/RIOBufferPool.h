#pragma once
// English: Windows RIO buffer pool — alias for AsyncBufferPool.
// 한글: Windows RIO 버퍼 풀 — AsyncBufferPool의 플랫폼 별칭.

#include "Platforms/AsyncBufferPool.h"

#ifdef _WIN32
namespace Network
{
namespace AsyncIO
{
namespace Windows
{
    using RIOBufferPool = ::Network::AsyncIO::AsyncBufferPool;
} // namespace Windows
} // namespace AsyncIO
} // namespace Network
#endif // _WIN32
