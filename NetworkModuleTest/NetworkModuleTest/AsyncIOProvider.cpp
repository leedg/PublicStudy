#include "AsyncIOProvider.h"
#include "PlatformDetect.h"

// Forward declarations - each factory lives in its platform sub-namespace
#ifdef _WIN32
namespace Network::AsyncIO::Windows
{
	std::unique_ptr<AsyncIOProvider> CreateIocpProvider();
	std::unique_ptr<AsyncIOProvider> CreateRIOProvider();
}
#endif
#ifdef __linux__
namespace Network::AsyncIO::Linux
{
	std::unique_ptr<AsyncIOProvider> CreateEpollProvider();
	std::unique_ptr<AsyncIOProvider> CreateIOUringProvider();
}
#endif
#ifdef __APPLE__
namespace Network::AsyncIO::MacOS
{
	std::unique_ptr<AsyncIOProvider> CreateKqueueProvider();
}
#endif

namespace Network::AsyncIO
{

	// =============================================================================
	// Factory Function Implementations
	// =============================================================================

	std::unique_ptr<AsyncIOProvider> CreateAsyncIOProvider(bool preferHighPerformance)
	{
		// Get the default backend for current platform
		// Windows -> PlatformType::IOCP (default backend)
		// Linux -> PlatformType::Epoll (default backend)
		// macOS -> PlatformType::Kqueue (default backend)
		PlatformType platform = GetCurrentPlatform();

		switch (platform)
		{
#ifdef _WIN32
		case PlatformType::IOCP:
		case PlatformType::RIO:
		{
			// Windows fallback chain:
			// 1. Try RIO (high-performance, Windows 8+)
			// 2. Fall back to IOCP (stable, all Windows)
			// 3. Return nullptr (fatal failure)

			if (preferHighPerformance && Platform::IsWindowsRIOSupported())
			{
				auto provider = Windows::CreateRIOProvider();
				if (provider && provider->Initialize())
					return provider;  // RIO success
				// RIO initialization failed -> try IOCP next
			}

			// Use IOCP (always available on Windows)
			auto provider = Windows::CreateIocpProvider();
			if (provider && provider->Initialize())
				return provider;  // IOCP success

			// Both RIO and IOCP failed -> fatal error
			return nullptr;
		}
#endif

#ifdef __linux__
		case PlatformType::Epoll:
		case PlatformType::IOUring:
		{
			// Linux fallback chain:
			// 1. Try io_uring (high-performance, kernel 5.1+)
			// 2. Fall back to epoll (stable, all Linux)
			// 3. Return nullptr (fatal failure)

			if (preferHighPerformance && Platform::IsLinuxIOUringSupported())
			{
				auto provider = Linux::CreateIOUringProvider();
				if (provider && provider->Initialize())
					return provider;  // io_uring success
				// io_uring initialization failed -> try epoll next
			}

			// Use epoll (always available on Linux)
			auto provider = Linux::CreateEpollProvider();
			if (provider && provider->Initialize())
				return provider;  // epoll success

			// Both io_uring and epoll failed -> fatal error
			return nullptr;
		}
#endif

#ifdef __APPLE__
		case PlatformType::Kqueue:
		{
			auto provider = MacOS::CreateKqueueProvider();
			if (provider && provider->Initialize())
				return provider;  // kqueue success

			return nullptr;
		}
#endif

		default:
			return nullptr;
		}
	}

	std::unique_ptr<AsyncIOProvider> CreateAsyncIOProviderForPlatform(PlatformType platformType)
	{
		// Create a specific backend implementation
		// This function bypasses the fallback chain and tries ONLY the requested backend
		// Use this for testing or when you explicitly want a specific implementation

		switch (platformType)
		{
#ifdef _WIN32
		case PlatformType::IOCP:
		{
			auto provider = Windows::CreateIocpProvider();
			if (provider && provider->Initialize())
				return provider;
			break;
		}

		case PlatformType::RIO:
		{
			auto provider = Windows::CreateRIOProvider();
			if (provider && provider->Initialize())
				return provider;
			break;
		}
#endif

#ifdef __linux__
		case PlatformType::Epoll:
		{
			auto provider = Linux::CreateEpollProvider();
			if (provider && provider->Initialize())
				return provider;
			break;
		}

		case PlatformType::IOUring:
		{
			auto provider = Linux::CreateIOUringProvider();
			if (provider && provider->Initialize())
				return provider;
			break;
		}
#endif

#ifdef __APPLE__
		case PlatformType::Kqueue:
		{
			auto provider = MacOS::CreateKqueueProvider();
			if (provider && provider->Initialize())
				return provider;
			break;
		}
#endif

		default:
			return nullptr;
		}

		// Requested backend initialization failed
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

}  // namespace Network::AsyncIO
