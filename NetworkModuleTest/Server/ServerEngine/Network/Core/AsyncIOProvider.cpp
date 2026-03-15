// Factory function implementations for AsyncIOProvider

#include "AsyncIOProvider.h"
#include "PlatformDetect.h"
#include <cstring>

// Forward declarations - each factory lives in its platform
#ifdef _WIN32
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
#ifdef __linux__
namespace Network
{
namespace AsyncIO
{
namespace Linux
{
std::unique_ptr<AsyncIOProvider> CreateEpollProvider();
#ifdef NETWORK_ENABLE_IO_URING
std::unique_ptr<AsyncIOProvider> CreateIOUringProvider();
#endif
} // namespace Linux
} // namespace AsyncIO
} // namespace Network
#endif
#ifdef __APPLE__
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

// =============================================================================
// Factory Function Implementations
// =============================================================================

std::unique_ptr<AsyncIOProvider> CreateAsyncIOProvider()
{
	// Get the default backend for current platform
	PlatformType platform = GetCurrentPlatform();

	switch (platform)
	{
#ifdef _WIN32
	case PlatformType::IOCP:
	case PlatformType::RIO:
	{
		// Windows fallback chain: RIO -> IOCP -> nullptr

		// Try RIO first (high-performance, Windows 8+)
		if (Platform::IsWindowsRIOSupported())
		{
			auto provider = Windows::CreateRIOProvider();
			if (provider)
				return provider;
			// RIO creation failed -> try IOCP next
		}

		// Use IOCP (always available on Windows)
		auto provider = Windows::CreateIocpProvider();
		if (provider)
			return provider;

		// Both RIO and IOCP failed -> fatal error
		return nullptr;
	}
#endif

#ifdef __linux__
	case PlatformType::Epoll:
	case PlatformType::IOUring:
	{
		// Linux fallback chain: io_uring -> epoll -> nullptr

#ifdef NETWORK_ENABLE_IO_URING
		// Try io_uring first (high-performance, kernel 5.1+)
		if (Platform::IsLinuxIOUringSupported())
		{
			auto provider = Linux::CreateIOUringProvider();
			if (provider)
				return provider;
			// io_uring creation failed -> try epoll next
		}
#endif

		// Use epoll (always available on Linux)
		auto provider = Linux::CreateEpollProvider();
		if (provider)
			return provider;

		// Both io_uring and epoll failed -> fatal error
		return nullptr;
	}
#endif

#ifdef __APPLE__
	case PlatformType::Kqueue:
	{
		// macOS: kqueue only (no fallback)
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
	// Create a specific backend implementation by name

	if (!platformHint)
		return nullptr;

#ifdef _WIN32
	if (std::strcmp(platformHint, "IOCP") == 0)
	{
		return Windows::CreateIocpProvider();
	}
	if (std::strcmp(platformHint, "RIO") == 0)
	{
		return Windows::CreateRIOProvider();
	}
#endif

#ifdef __linux__
	if (std::strcmp(platformHint, "epoll") == 0)
	{
		return Linux::CreateEpollProvider();
	}
#ifdef NETWORK_ENABLE_IO_URING
	if (std::strcmp(platformHint, "io_uring") == 0)
	{
		return Linux::CreateIOUringProvider();
	}
#endif
#endif

#ifdef __APPLE__
	if (std::strcmp(platformHint, "kqueue") == 0)
	{
		return BSD::CreateKqueueProvider();
	}
#endif

	return nullptr;
}

bool IsPlatformSupported(const char *platformHint)
{
	// Check if a specific platform is supported

	if (!platformHint)
		return false;

#ifdef _WIN32
	if (std::strcmp(platformHint, "IOCP") == 0)
		return true;
	if (std::strcmp(platformHint, "RIO") == 0)
		return Platform::IsWindowsRIOSupported();
#endif

#ifdef __linux__
	if (std::strcmp(platformHint, "epoll") == 0)
		return Platform::IsLinuxEpollSupported();
#ifdef NETWORK_ENABLE_IO_URING
	if (std::strcmp(platformHint, "io_uring") == 0)
		return Platform::IsLinuxIOUringSupported();
#endif
#endif

#ifdef __APPLE__
	if (std::strcmp(platformHint, "kqueue") == 0)
		return Platform::IsMacOSKqueueSupported();
#endif

	return false;
}

// Static storage for supported platform names
static const char *sSupportedPlatforms[] = {
#ifdef _WIN32
	"IOCP",   "RIO",
#endif
#ifdef __linux__
	"epoll",
#ifdef NETWORK_ENABLE_IO_URING
	"io_uring",
#endif
#endif
#ifdef __APPLE__
	"kqueue",
#endif
};

const char **GetSupportedPlatforms(size_t &outCount)
{
	// Return array of supported platform names
	outCount = sizeof(sSupportedPlatforms) / sizeof(sSupportedPlatforms[0]);
	return const_cast<const char **>(sSupportedPlatforms);
}

PlatformType GetCurrentPlatform()
{
	// Delegate to platform detection
	return Platform::DetectPlatform();
}

PlatformInfo GetPlatformInfo()
{
	// Delegate to detailed platform info
	return Platform::GetDetailedPlatformInfo();
}

} // namespace AsyncIO
} // namespace Network
