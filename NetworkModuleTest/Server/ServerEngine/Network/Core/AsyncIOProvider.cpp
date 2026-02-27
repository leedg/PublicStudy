// English: Factory function implementations for AsyncIOProvider
// 한글: AsyncIOProvider 팩토리 함수 구현

#include "AsyncIOProvider.h"
#include "PlatformDetect.h"
#include <cstring>

// English: Forward declarations - each factory lives in its platform
// sub-namespace 한글: 전방 선언 - 각 팩토리는 플랫폼 하위 네임스페이스에 존재
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
// 한글: 팩토리 함수 구현
// =============================================================================

std::unique_ptr<AsyncIOProvider> CreateAsyncIOProvider()
{
	// English: Get the default backend for current platform
	// 한글: 현재 플랫폼의 기본 백엔드 조회
	// Windows -> PlatformType::IOCP (기본 백엔드)
	// Linux -> PlatformType::Epoll (기본 백엔드)
	// macOS -> PlatformType::Kqueue (기본 백엔드)
	PlatformType platform = GetCurrentPlatform();

	switch (platform)
	{
#ifdef _WIN32
	case PlatformType::IOCP:
	case PlatformType::RIO:
	{
		// English: Windows fallback chain: RIO -> IOCP -> nullptr
		// 한글: Windows 폴백 체인: RIO -> IOCP -> nullptr

		// English: Try RIO first (high-performance, Windows 8+)
		// 한글: 먼저 RIO 시도 (고성능, Windows 8+)
		if (Platform::IsWindowsRIOSupported())
		{
			auto provider = Windows::CreateRIOProvider();
			if (provider)
				return provider;
			// English: RIO creation failed -> try IOCP next
			// 한글: RIO 생성 실패 -> 다음 IOCP 시도
		}

		// English: Use IOCP (always available on Windows)
		// 한글: IOCP 사용 (Windows에서 항상 사용 가능)
		auto provider = Windows::CreateIocpProvider();
		if (provider)
			return provider;

		// English: Both RIO and IOCP failed -> fatal error
		// 한글: RIO와 IOCP 모두 실패 -> 치명적 에러
		return nullptr;
	}
#endif

#ifdef __linux__
	case PlatformType::Epoll:
	case PlatformType::IOUring:
	{
		// English: Linux fallback chain: io_uring -> epoll -> nullptr
		// 한글: Linux 폴백 체인: io_uring -> epoll -> nullptr

		// English: Try io_uring first (high-performance, kernel 5.1+)
		// 한글: 먼저 io_uring 시도 (고성능, 커널 5.1+)
		if (Platform::IsLinuxIOUringSupported())
		{
			auto provider = Linux::CreateIOUringProvider();
			if (provider)
				return provider;
			// English: io_uring creation failed -> try epoll next
			// 한글: io_uring 생성 실패 -> 다음 epoll 시도
		}

		// English: Use epoll (always available on Linux)
		// 한글: epoll 사용 (Linux에서 항상 사용 가능)
		auto provider = Linux::CreateEpollProvider();
		if (provider)
			return provider;

		// English: Both io_uring and epoll failed -> fatal error
		// 한글: io_uring과 epoll 모두 실패 -> 치명적 에러
		return nullptr;
	}
#endif

#ifdef __APPLE__
	case PlatformType::Kqueue:
	{
		// English: macOS: kqueue only (no fallback)
		// 한글: macOS: kqueue만 사용 (폴백 없음)
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
	// 한글: 이름으로 특정 백엔드 구현 생성

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
	// 한글: 특정 플랫폼 지원 여부 확인

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
// 한글: 지원 플랫폼 이름의 정적 저장소
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
	// 한글: 지원 플랫폼 이름 배열 반환
	outCount = sizeof(sSupportedPlatforms) / sizeof(sSupportedPlatforms[0]);
	return sSupportedPlatforms;
}

PlatformType GetCurrentPlatform()
{
	// English: Delegate to platform detection
	// 한글: 플랫폼 감지에 위임
	return Platform::DetectPlatform();
}

PlatformInfo GetPlatformInfo()
{
	// English: Delegate to detailed platform info
	// 한글: 상세 플랫폼 정보에 위임
	return Platform::GetDetailedPlatformInfo();
}

} // namespace AsyncIO
} // namespace Network
