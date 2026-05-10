// English: Factory function implementations for AsyncIOProvider
// 한글: AsyncIOProvider 팩토리 함수 구현

#include "AsyncIOProvider.h"
#include "PlatformDetect.h"
#include <cstring>

// English: Forward declarations - each factory lives in its platform
// sub-namespace 한글: 전방 선언 - 각 팩토리는 플랫폼 하위 네임스페이스에 존재
#if defined(IS_WINDOWS)
namespace Network
{
namespace AsyncIO
{
namespace Windows
{
std::unique_ptr<AsyncIOProvider> CreateIocpProvider();
std::unique_ptr<AsyncIOProvider> CreateRIOProvider();
} // namespace Windows
} // namespace AsyncIO
} // namespace Network
#endif
#if defined(IS_LINUX)
namespace Network
{
namespace AsyncIO
{
namespace Linux
{
std::unique_ptr<AsyncIOProvider> CreateEpollProvider();
#if defined(HAS_IO_URING)
std::unique_ptr<AsyncIOProvider> CreateIOUringProvider();
#endif
} // namespace Linux
} // namespace AsyncIO
} // namespace Network
#endif
#if defined(IS_MACOS)
namespace Network
{
namespace AsyncIO
{
namespace BSD
{
std::unique_ptr<AsyncIOProvider> CreateKqueueProvider();
}
} // namespace AsyncIO
} // namespace Network
#endif

namespace Network
{
namespace AsyncIO
{

std::unique_ptr<AsyncIOProvider> CreateAsyncIOProvider()
{
    PlatformType platform = GetCurrentPlatform();

    switch (platform)
    {
#if defined(IS_WINDOWS)
    case PlatformType::IOCP:
    case PlatformType::RIO:
    {
        if (Platform::IsWindowsRIOSupported())
        {
            auto provider = Windows::CreateRIOProvider();
            if (provider)
                return provider;
        }

        auto provider = Windows::CreateIocpProvider();
        if (provider)
            return provider;

        return nullptr;
    }
#endif

#if defined(IS_LINUX)
    case PlatformType::Epoll:
    case PlatformType::IOUring:
    {
#if defined(HAS_IO_URING)
        if (Platform::IsLinuxIOUringSupported())
        {
            auto provider = Linux::CreateIOUringProvider();
            if (provider)
                return provider;
        }
#endif

        auto provider = Linux::CreateEpollProvider();
        if (provider)
            return provider;

        return nullptr;
    }
#endif

#if defined(IS_MACOS)
    case PlatformType::Kqueue:
    {
        auto provider = BSD::CreateKqueueProvider();
        if (provider)
            return provider;

        return nullptr;
    }
#endif

    default:
        return nullptr;
    }
}

std::unique_ptr<AsyncIOProvider> CreateAsyncIOProvider(const char *platformHint)
{
    if (!platformHint)
        return nullptr;

#if defined(IS_WINDOWS)
    if (std::strcmp(platformHint, "IOCP") == 0)
    {
        return Windows::CreateIocpProvider();
    }
    if (std::strcmp(platformHint, "RIO") == 0)
    {
        return Windows::CreateRIOProvider();
    }
#endif

#if defined(IS_LINUX)
    if (std::strcmp(platformHint, "epoll") == 0)
    {
        return Linux::CreateEpollProvider();
    }
#if defined(HAS_IO_URING)
    if (std::strcmp(platformHint, "io_uring") == 0)
    {
        return Linux::CreateIOUringProvider();
    }
#endif
#endif

#if defined(IS_MACOS)
    if (std::strcmp(platformHint, "kqueue") == 0)
    {
        return BSD::CreateKqueueProvider();
    }
#endif

    return nullptr;
}

bool IsPlatformSupported(const char *platformHint)
{
    if (!platformHint)
        return false;

#if defined(IS_WINDOWS)
    if (std::strcmp(platformHint, "IOCP") == 0)
        return true;
    if (std::strcmp(platformHint, "RIO") == 0)
        return Platform::IsWindowsRIOSupported();
#endif

#if defined(IS_LINUX)
    if (std::strcmp(platformHint, "epoll") == 0)
        return Platform::IsLinuxEpollSupported();
#if defined(HAS_IO_URING)
    if (std::strcmp(platformHint, "io_uring") == 0)
        return Platform::IsLinuxIOUringSupported();
#endif
#endif

#if defined(IS_MACOS)
    if (std::strcmp(platformHint, "kqueue") == 0)
        return Platform::IsMacOSKqueueSupported();
#endif

    return false;
}

static const char *sSupportedPlatforms[] = {
#if defined(IS_WINDOWS)
    "IOCP",   "RIO",
#endif
#if defined(IS_LINUX)
    "epoll",
#if defined(HAS_IO_URING)
    "io_uring",
#endif
#endif
#if defined(IS_MACOS)
    "kqueue",
#endif
};

const char **GetSupportedPlatforms(size_t &outCount)
{
    outCount = sizeof(sSupportedPlatforms) / sizeof(sSupportedPlatforms[0]);
    return const_cast<const char **>(sSupportedPlatforms);
}

PlatformType GetCurrentPlatform()
{
    return Platform::DetectPlatform();
}

PlatformInfo GetPlatformInfo()
{
    return Platform::GetDetailedPlatformInfo();
}

} // namespace AsyncIO
} // namespace Network
