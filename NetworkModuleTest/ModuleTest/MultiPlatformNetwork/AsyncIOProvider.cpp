// English: Factory function implementations for AsyncIOProvider
// ?쒓?: AsyncIOProvider ?⑺넗由??⑥닔 援ы쁽

#include "AsyncIOProvider.h"
#include "PlatformDetect.h"
#include <cstring>

// English: Forward declarations - each factory lives in its platform
// sub-namespace ?쒓?: ?꾨갑 ?좎뼵 - 媛??⑺넗由щ뒗 ?뚮옯???섏쐞
// ?ㅼ엫?ㅽ럹?댁뒪??議댁옱
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
std::unique_ptr<AsyncIOProvider> CreateIOUringProvider();
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
// English: Factory Function Implementations
// ?쒓?: ?⑺넗由??⑥닔 援ы쁽
// =============================================================================

std::unique_ptr<AsyncIOProvider> CreateAsyncIOProvider()
{
	// English: Get the default backend for current platform
	// ?쒓?: ?꾩옱 ?뚮옯?쇱쓽 湲곕낯 諛깆뿏??議고쉶
	// Windows -> PlatformType::IOCP (湲곕낯 諛깆뿏??
	// Linux -> PlatformType::Epoll (湲곕낯 諛깆뿏??
	// macOS -> PlatformType::Kqueue (湲곕낯 諛깆뿏??
	PlatformType platform = GetCurrentPlatform();

	switch (platform)
	{
#ifdef _WIN32
	case PlatformType::IOCP:
	case PlatformType::RIO:
	{
		// English: Windows fallback chain: RIO -> IOCP -> nullptr
		// ?쒓?: Windows ?대갚 泥댁씤: RIO -> IOCP -> nullptr

		// English: Try RIO first (high-performance, Windows 8+)
		// ?쒓?: 癒쇱? RIO ?쒕룄 (怨좎꽦?? Windows 8+)
		if (Platform::IsWindowsRIOSupported())
		{
			auto provider = Windows::CreateRIOProvider();
			if (provider)
				return provider;
			// English: RIO creation failed -> try IOCP next
			// ?쒓?: RIO ?앹꽦 ?ㅽ뙣 -> ?ㅼ쓬 IOCP ?쒕룄
		}

		// English: Use IOCP (always available on Windows)
		// ?쒓?: IOCP ?ъ슜 (Windows?먯꽌 ??긽 ?ъ슜 媛??
		auto provider = Windows::CreateIocpProvider();
		if (provider)
			return provider;

		// English: Both RIO and IOCP failed -> fatal error
		// ?쒓?: RIO? IOCP 紐⑤몢 ?ㅽ뙣 -> 移섎챸???먮윭
		return nullptr;
	}
#endif

#ifdef __linux__
	case PlatformType::Epoll:
	case PlatformType::IOUring:
	{
		// English: Linux fallback chain: io_uring -> epoll -> nullptr
		// ?쒓?: Linux ?대갚 泥댁씤: io_uring -> epoll -> nullptr

		// English: Try io_uring first (high-performance, kernel 5.1+)
		// ?쒓?: 癒쇱? io_uring ?쒕룄 (怨좎꽦?? 而ㅻ꼸 5.1+)
		if (Platform::IsLinuxIOUringSupported())
		{
			auto provider = Linux::CreateIOUringProvider();
			if (provider)
				return provider;
			// English: io_uring creation failed -> try epoll next
			// ?쒓?: io_uring ?앹꽦 ?ㅽ뙣 -> ?ㅼ쓬 epoll ?쒕룄
		}

		// English: Use epoll (always available on Linux)
		// ?쒓?: epoll ?ъ슜 (Linux?먯꽌 ??긽 ?ъ슜 媛??
		auto provider = Linux::CreateEpollProvider();
		if (provider)
			return provider;

		// English: Both io_uring and epoll failed -> fatal error
		// ?쒓?: io_uring怨?epoll 紐⑤몢 ?ㅽ뙣 -> 移섎챸???먮윭
		return nullptr;
	}
#endif

#ifdef __APPLE__
	case PlatformType::Kqueue:
	{
		// English: macOS: kqueue only (no fallback)
		// ?쒓?: macOS: kqueue留??ъ슜 (?대갚 ?놁쓬)
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
	// English: Create a specific backend implementation by name
	// ?쒓?: ?대쫫?쇰줈 ?뱀젙 諛깆뿏??援ы쁽 ?앹꽦

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
	if (std::strcmp(platformHint, "io_uring") == 0)
	{
		return Linux::CreateIOUringProvider();
	}
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
	// English: Check if a specific platform is supported
	// ?쒓?: ?뱀젙 ?뚮옯??吏???щ? ?뺤씤

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
	if (std::strcmp(platformHint, "io_uring") == 0)
		return Platform::IsLinuxIOUringSupported();
#endif

#ifdef __APPLE__
	if (std::strcmp(platformHint, "kqueue") == 0)
		return Platform::IsMacOSKqueueSupported();
#endif

	return false;
}

// English: Static storage for supported platform names
// ?쒓?: 吏???뚮옯???대쫫???뺤쟻 ??μ냼
static const char *sSupportedPlatforms[] = {
#ifdef _WIN32
	"IOCP",   "RIO",
#endif
#ifdef __linux__
	"epoll",  "io_uring",
#endif
#ifdef __APPLE__
	"kqueue",
#endif
};

const char **GetSupportedPlatforms(size_t &outCount)
{
	// English: Return array of supported platform names
	// ?쒓?: 吏???뚮옯???대쫫 諛곗뿴 諛섑솚
	outCount = sizeof(sSupportedPlatforms) / sizeof(sSupportedPlatforms[0]);
	return const_cast<const char **>(sSupportedPlatforms);
}

PlatformType GetCurrentPlatform()
{
	// English: Delegate to platform detection
	// ?쒓?: ?뚮옯??媛먯????꾩엫
	return Platform::DetectPlatform();
}

PlatformInfo GetPlatformInfo()
{
	// English: Delegate to detailed platform info
	// ?쒓?: ?곸꽭 ?뚮옯???뺣낫???꾩엫
	return Platform::GetDetailedPlatformInfo();
}

} // namespace AsyncIO
} // namespace Network
