// AsyncIOProvider 팩토리 함수 구현

#include "AsyncIOProvider.h"
#include "PlatformDetect.h"
#include <cstring>

// 전방 선언 — 각 팩토리는 플랫폼 하위 네임스페이스에 존재
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
// 팩토리 함수 구현
// =============================================================================

std::unique_ptr<AsyncIOProvider> CreateAsyncIOProvider()
{
	// 현재 플랫폼의 기본 백엔드 조회 (Windows=IOCP, Linux=Epoll, macOS=Kqueue)
	PlatformType platform = GetCurrentPlatform();

	switch (platform)
	{
#ifdef _WIN32
	case PlatformType::IOCP:
	case PlatformType::RIO:
	{
		// Windows 폴백 체인: RIO -> IOCP -> nullptr
		if (Platform::IsWindowsRIOSupported())
		{
			auto provider = Windows::CreateRIOProvider();
			if (provider)
				return provider;
			// RIO 생성 실패 -> IOCP 시도
		}

		// IOCP는 모든 Windows 버전에서 사용 가능
		auto provider = Windows::CreateIocpProvider();
		if (provider)
			return provider;

		return nullptr;
	}
#endif

#ifdef __linux__
	case PlatformType::Epoll:
	case PlatformType::IOUring:
	{
		// Linux 폴백 체인: io_uring -> epoll -> nullptr
#ifdef NETWORK_ENABLE_IO_URING
		if (Platform::IsLinuxIOUringSupported())
		{
			auto provider = Linux::CreateIOUringProvider();
			if (provider)
				return provider;
			// io_uring 생성 실패 -> epoll 시도
		}
#endif

		// epoll은 거의 모든 Linux에서 사용 가능
		auto provider = Linux::CreateEpollProvider();
		if (provider)
			return provider;

		return nullptr;
	}
#endif

#ifdef __APPLE__
	case PlatformType::Kqueue:
	{
		// macOS: kqueue만 사용 (폴백 없음)
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
	// 이름으로 특정 백엔드 구현 생성

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
	// 특정 플랫폼 지원 여부 확인

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

// 지원 플랫폼 이름의 정적 저장소 (GetSupportedPlatforms 반환값의 수명을 보장하기 위해 static)
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
	// 지원 플랫폼 이름 배열 반환
	outCount = sizeof(sSupportedPlatforms) / sizeof(sSupportedPlatforms[0]);
	return const_cast<const char **>(sSupportedPlatforms);
}

PlatformType GetCurrentPlatform()
{
	// 플랫폼 감지에 위임
	return Platform::DetectPlatform();
}

PlatformInfo GetPlatformInfo()
{
	// 상세 플랫폼 정보에 위임
	return Platform::GetDetailedPlatformInfo();
}

} // namespace AsyncIO
} // namespace Network
