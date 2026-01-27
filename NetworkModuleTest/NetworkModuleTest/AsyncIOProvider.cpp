#include "AsyncIOProvider.h"
#include "PlatformDetect.h"

namespace RAON::Network::AsyncIO
{
    // Forward declarations for platform-specific implementations
    // (Will be implemented in separate files)
    std::unique_ptr<AsyncIOProvider> CreateIocpProvider();
    std::unique_ptr<AsyncIOProvider> CreateRIOProvider();
    std::unique_ptr<AsyncIOProvider> CreateEpollProvider();
    std::unique_ptr<AsyncIOProvider> CreateIOUringProvider();
    std::unique_ptr<AsyncIOProvider> CreateKqueueProvider();

    // =============================================================================
    // Factory Function Implementations
    // =============================================================================

    std::unique_ptr<AsyncIOProvider> CreateAsyncIOProvider(bool preferHighPerformance)
    {
        PlatformType platform = GetCurrentPlatform();
        
        switch (platform)
        {
#ifdef _WIN32
        case PlatformType::IOCP:
        {
            // Try RIO first if high performance is preferred
            if (preferHighPerformance && Platform::IsWindowsRIOSupported())
            {
                auto provider = CreateRIOProvider();
                if (provider && provider->Initialize())
                    return provider;
            }
            
            // Fall back to IOCP
            auto provider = CreateIocpProvider();
            if (provider && provider->Initialize())
                return provider;
            
            return nullptr;
        }
#endif

#ifdef __linux__
        case PlatformType::Epoll:
        {
            // Try io_uring first if high performance is preferred
            if (preferHighPerformance && Platform::IsLinuxIOUringSupported())
            {
                auto provider = CreateIOUringProvider();
                if (provider && provider->Initialize())
                    return provider;
            }
            
            // Fall back to epoll
            auto provider = CreateEpollProvider();
            if (provider && provider->Initialize())
                return provider;
            
            return nullptr;
        }
#endif

#ifdef __APPLE__
        case PlatformType::Kqueue:
        {
            auto provider = CreateKqueueProvider();
            if (provider && provider->Initialize())
                return provider;
            
            return nullptr;
        }
#endif

        default:
            return nullptr;
        }
    }

    std::unique_ptr<AsyncIOProvider> CreateAsyncIOProviderForPlatform(PlatformType platformType)
    {
        switch (platformType)
        {
        case PlatformType::IOCP:
        {
            auto provider = CreateIocpProvider();
            if (provider && provider->Initialize())
                return provider;
            break;
        }

        case PlatformType::RIO:
        {
            auto provider = CreateRIOProvider();
            if (provider && provider->Initialize())
                return provider;
            break;
        }

        case PlatformType::Epoll:
        {
            auto provider = CreateEpollProvider();
            if (provider && provider->Initialize())
                return provider;
            break;
        }

        case PlatformType::IOUring:
        {
            auto provider = CreateIOUringProvider();
            if (provider && provider->Initialize())
                return provider;
            break;
        }

        case PlatformType::Kqueue:
        {
            auto provider = CreateKqueueProvider();
            if (provider && provider->Initialize())
                return provider;
            break;
        }

        default:
            return nullptr;
        }

        return nullptr;
    }

    PlatformType GetCurrentPlatform()
    {
        return Platform::DetectPlatform();
    }

    PlatformInfo GetPlatformInfo()
    {
        return Platform::GetDetailedPlatformInfo();
    }

}  // namespace RAON::Network::AsyncIO
