#include "AsyncIOProvider.h"
#include "PlatformDetect.h"

namespace Network::AsyncIO
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
		// Get the default backend for current platform
		// Windows → PlatformType::IOCP (default backend)
		// Linux → PlatformType::Epoll (default backend)
		// macOS → PlatformType::Kqueue (default backend)
		PlatformType platform = GetCurrentPlatform();

		switch (platform)
		{
#ifdef _WIN32
		case PlatformType::IOCP:  // Windows platform detected (default backend = IOCP)
		{
			// Fallback chain on Windows:
			// 1. Try RIO (high-performance, Windows 8+)
			// 2. Fall back to IOCP (stable, all Windows)
			// 3. Return nullptr (fatal failure)

			if (preferHighPerformance && Platform::IsWindowsRIOSupported())
			{
				auto provider = CreateRIOProvider();
				if (provider && provider->Initialize())
					return provider;  // RIO success
				// RIO initialization failed → try IOCP next
			}

			// Use IOCP (always available on Windows)
			auto provider = CreateIocpProvider();
			if (provider && provider->Initialize())
				return provider;  // IOCP success

			// Both RIO and IOCP failed → fatal error
			return nullptr;
		}
#endif

#ifdef __linux__
		case PlatformType::Epoll:  // Linux platform detected (default backend = epoll)
		{
			// Fallback chain on Linux:
			// 1. Try io_uring (high-performance, kernel 5.1+)
			// 2. Fall back to epoll (stable, all Linux)
			// 3. Return nullptr (fatal failure)

			if (preferHighPerformance && Platform::IsLinuxIOUringSupported())
			{
				auto provider = CreateIOUringProvider();
				if (provider && provider->Initialize())
					return provider;  // io_uring success
				// io_uring initialization failed → try epoll next
			}

			// Use epoll (always available on Linux)
			auto provider = CreateEpollProvider();
			if (provider && provider->Initialize())
				return provider;  // epoll success

			// Both io_uring and epoll failed → fatal error
			return nullptr;
		}
#endif

#ifdef __APPLE__
		case PlatformType::Kqueue:  // macOS platform detected (standard backend = kqueue)
		{
			// Fallback chain on macOS:
			// 1. Try kqueue (only option, always available)
			// 2. Return nullptr (fatal failure)
			// Note: preferHighPerformance is ignored on macOS (kqueue is both perf & standard)

			auto provider = CreateKqueueProvider();
			if (provider && provider->Initialize())
				return provider;  // kqueue success

			// kqueue initialization failed → fatal error
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
