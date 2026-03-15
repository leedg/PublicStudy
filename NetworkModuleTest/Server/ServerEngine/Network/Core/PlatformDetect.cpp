// Platform detection implementation

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
// Platform Detection Implementation
// =============================================================================

PlatformType DetectPlatform()
{
	// Return default backend for current OS
#ifdef _WIN32
	return PlatformType::IOCP; // Windows default
#elif __APPLE__
	return PlatformType::Kqueue; // macOS default
#elif __linux__
	return PlatformType::Epoll; // Linux default
#else
	return PlatformType::IOCP; // Fallback
#endif
}

PlatformInfo GetDetailedPlatformInfo()
{
	// Build detailed platform info structure
	PlatformInfo info{};

#ifdef _WIN32
	// Windows Platform
	info.mPlatformType = PlatformType::IOCP;
	info.mPlatformName = "Windows";

	// Detect Windows version
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

	// Check RIO support (Windows 8+)
	info.mSupportRIO = IsWindows8OrGreater();
	info.mSupportIOUring = false;
	info.mSupportKqueue = false;

#elif __APPLE__
	// macOS Platform
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
	info.mSupportKqueue = true; // Always supported

#elif __linux__
	// Linux Platform
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
	// Unknown platform
	info.mPlatformType = PlatformType::IOCP;
	info.mPlatformName = "Unknown";
#endif

	return info;
}

bool IsWindowsRIOSupported()
{
	// RIO is supported on Windows 8 and later
#ifdef _WIN32
	return IsWindows8OrGreater();
#else
	return false;
#endif
}

bool IsLinuxIOUringSupported()
{
	// io_uring requires Linux kernel 5.1+
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
	// epoll is supported on virtually all modern Linux
#ifdef __linux__
	return true;
#else
	return false;
#endif
}

bool IsMacOSKqueueSupported()
{
	// kqueue is supported on all macOS versions
#ifdef __APPLE__
	return true;
#else
	return false;
#endif
}

uint32_t GetWindowsMajorVersion()
{
	// Return Windows major version number
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
	// Parse Linux kernel version from uname
#ifdef __linux__
	struct utsname buf{};
	if (uname(&buf) != 0)
		return false;

	// Parse version string like "5.10.0-8-generic"
	int parsed =
		sscanf(buf.release, "%u.%u.%u", &outMajor, &outMinor, &outPatch);
	return parsed >= 2; // At least major.minor required
#else
	return false;
#endif
}

bool GetMacOSVersion(uint32_t &outMajor, uint32_t &outMinor, uint32_t &outPatch)
{
	// Get macOS version via sysctl
#ifdef __APPLE__
	int mib[2] = {CTL_KERN, KERN_OSRELEASE};
	char release[256] = {0};
	size_t len = sizeof(release);

	if (sysctl(mib, 2, release, &len, nullptr, 0) != 0)
		return false;

	// Parse version string like "20.6.0"
	int parsed = sscanf(release, "%u.%u.%u", &outMajor, &outMinor, &outPatch);
	return parsed >= 2;
#else
	return false;
#endif
}

} // namespace Platform
} // namespace AsyncIO
} // namespace Network
