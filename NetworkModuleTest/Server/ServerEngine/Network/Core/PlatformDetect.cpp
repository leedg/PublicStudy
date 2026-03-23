// 플랫폼 감지 구현

#include "PlatformDetect.h"
#include <cstdio>
#include <cstring>
#include <cstdio>

#ifdef _WIN32
#include <versionhelpers.h>
#include <windows.h>
#elif __APPLE__
#include <TargetConditionals.h>
#include <sys/sysctl.h>
#else // Linux
#include <sys/utsname.h>
#endif

namespace Network
{
namespace AsyncIO
{
namespace Platform
{
// =============================================================================
// 플랫폼 감지 구현
// =============================================================================

PlatformType DetectPlatform()
{
	// 각 OS의 기본(안정적) 백엔드를 반환.
	// 고성능 백엔드(RIO, io_uring) 선택은 CreateAsyncIOProvider()에서 폴백 체인으로 처리.
#ifdef _WIN32
	return PlatformType::IOCP;
#elif __APPLE__
	return PlatformType::Kqueue;
#elif __linux__
	return PlatformType::Epoll;
#else
	return PlatformType::IOCP; // 알 수 없는 플랫폼 폴백
#endif
}

PlatformInfo GetDetailedPlatformInfo()
{
	// 상세 플랫폼 정보 구조체 구성
	PlatformInfo info{};

#ifdef _WIN32
	info.mPlatformType = PlatformType::IOCP;
	info.mPlatformName = "Windows";

	// Windows 버전 감지 (versionhelpers.h API 사용)
	uint32_t major = 0;
	uint32_t minor = 0;

	if (IsWindows10OrGreater())
	{
		major = 10;
	}
	else if (IsWindows8Point1OrGreater())
	{
		major = 8;
		minor = 1;
	}
	else if (IsWindows8OrGreater())
	{
		major = 8;
	}
	else if (IsWindows7OrGreater())
	{
		major = 7;
	}
	else if (IsWindowsVistaOrGreater())
	{
		major = 6;
	}

	info.mMajorVersion = major;
	info.mMinorVersion = minor;

	// RIO는 Windows 8(커널 6.2)에서 도입됨
	info.mSupportRIO = IsWindows8OrGreater();
	info.mSupportIOUring = false;
	info.mSupportKqueue = false;

#elif __APPLE__
	info.mPlatformType = PlatformType::Kqueue;
	info.mPlatformName = "macOS";

	uint32_t major = 0;
	uint32_t minor = 0;
	uint32_t patch = 0;
	GetMacOSVersion(major, minor, patch);

	info.mMajorVersion = major;
	info.mMinorVersion = minor;
	info.mSupportRIO = false;
	info.mSupportIOUring = false;
	info.mSupportKqueue = true; // kqueue는 모든 macOS 버전에서 지원됨

#elif __linux__
	info.mPlatformType = PlatformType::Epoll;
	info.mPlatformName = "Linux";

	uint32_t major = 0;
	uint32_t minor = 0;
	uint32_t patch = 0;
	GetLinuxKernelVersion(major, minor, patch);

	info.mMajorVersion = major;
	info.mMinorVersion = minor;
	info.mSupportRIO = false;
	info.mSupportIOUring = IsLinuxIOUringSupported();
	info.mSupportKqueue = false;

#else
	info.mPlatformType = PlatformType::IOCP; // 알 수 없는 플랫폼 폴백
	info.mPlatformName = "Unknown";
#endif

	return info;
}

bool IsWindowsRIOSupported()
{
	// RIO는 Windows 8(커널 6.2) 이상에서 지원
#ifdef _WIN32
	return IsWindows8OrGreater();
#else
	return false;
#endif
}

bool IsLinuxIOUringSupported()
{
	// io_uring은 Linux 커널 5.1+에서 도입됨 (기본 사용 가능; 고급 기능은 더 높은 버전 필요)
#ifdef __linux__
	uint32_t major = 0;
	uint32_t minor = 0;
	uint32_t patch = 0;
	if (!GetLinuxKernelVersion(major, minor, patch))
		return false;

	if (major > 5)
		return true;
	if (major == 5 && minor >= 1)
		return true;

	return false;
#else
	return false;
#endif
}

bool IsLinuxEpollSupported()
{
	// epoll은 Linux 2.5.44+에서 도입되어 사실상 모든 현대 Linux에서 사용 가능
#ifdef __linux__
	return true;
#else
	return false;
#endif
}

bool IsMacOSKqueueSupported()
{
	// kqueue는 macOS 10.3 이후 모든 버전에서 지원됨
#ifdef __APPLE__
	return true;
#else
	return false;
#endif
}

uint32_t GetWindowsMajorVersion()
{
	// Windows 주 버전 번호 반환 (versionhelpers.h API 기반)
#ifdef _WIN32
	if (IsWindows10OrGreater())
		return 10;
	if (IsWindows8Point1OrGreater())
		return 8;
	if (IsWindows8OrGreater())
		return 8;
	if (IsWindows7OrGreater())
		return 7;
	if (IsWindowsVistaOrGreater())
		return 6;
	return 0;
#else
	return 0;
#endif
}

bool GetLinuxKernelVersion(uint32_t &outMajor, uint32_t &outMinor,
							   uint32_t &outPatch)
{
	// uname()으로 커널 릴리즈 문자열을 가져와 major.minor.patch 파싱
#ifdef __linux__
	struct utsname buf{};
	if (uname(&buf) != 0)
		return false;

	// "5.10.0-8-generic" 형식의 버전 문자열 파싱
	int parsed =
		sscanf(buf.release, "%u.%u.%u", &outMajor, &outMinor, &outPatch);
	return parsed >= 2; // major.minor 최소 요구; patch 없는 경우도 허용
#else
	return false;
#endif
}

bool GetMacOSVersion(uint32_t &outMajor, uint32_t &outMinor, uint32_t &outPatch)
{
	// sysctl(CTL_KERN, KERN_OSRELEASE)로 macOS 커널 릴리즈 버전 조회
#ifdef __APPLE__
	int mib[2] = {CTL_KERN, KERN_OSRELEASE};
	char release[256] = {0};
	size_t len = sizeof(release);

	if (sysctl(mib, 2, release, &len, nullptr, 0) != 0)
		return false;

	// "20.6.0" 형식의 버전 문자열 파싱 (XNU 커널 버전 — macOS 버전과 다름에 주의)
	int parsed = sscanf(release, "%u.%u.%u", &outMajor, &outMinor, &outPatch);
	return parsed >= 2;
#else
	return false;
#endif
}

} // namespace Platform
} // namespace AsyncIO
} // namespace Network
