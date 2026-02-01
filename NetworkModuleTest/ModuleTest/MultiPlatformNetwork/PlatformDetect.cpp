// English: Platform detection implementation
// Korean: Platform detection utilities implementation

#include "PlatformDetect.h"
#include <cstring>

#ifdef _WIN32
# include <windows.h>
# include <versionhelpers.h>
#elif __APPLE__
# include <TargetConditionals.h>
# include <sys/sysctl.h>
#else
# include <sys/utsname.h>
#endif

namespace Network { namespace AsyncIO { namespace Platform {

PlatformType DetectPlatform()
{
#ifdef _WIN32
    return PlatformType::IOCP;
#elif __APPLE__
    return PlatformType::Kqueue;
#elif __linux__
    return PlatformType::Epoll;
#else
    return PlatformType::IOCP;
#endif
}

PlatformInfo GetDetailedPlatformInfo()
{
    PlatformInfo info{};

#ifdef _WIN32
    info.mPlatformType = PlatformType::IOCP;
    info.mPlatformName = "Windows";
    uint32_t major = 0, minor = 0;
    if (IsWindows10OrGreater()) major = 10;
    else if (IsWindows8Point1OrGreater()) { major = 8; minor = 1; }
    else if (IsWindows8OrGreater()) major = 8;
    else if (IsWindows7OrGreater()) major = 7;
    else if (IsWindowsVistaOrGreater()) major = 6;
    info.mMajorVersion = major;
    info.mMinorVersion = minor;

    info.mSupportRIO = IsWindows8OrGreater();
    info.mSupportIOUring = false;
    info.mSupportKqueue = false;

#elif __APPLE__
    info.mPlatformType = PlatformType::Kqueue;
    info.mPlatformName = "macOS";
    uint32_t major=0, minor=0, patch=0;
    GetMacOSVersion(major, minor, patch);
    info.mMajorVersion = major;
    info.mMinorVersion = minor;
    info.mSupportRIO = false;
    info.mSupportIOUring = false;
    info.mSupportKqueue = true;

#elif __linux__
    info.mPlatformType = PlatformType::Epoll;
    info.mPlatformName = "Linux";
    uint32_t major=0, minor=0, patch=0;
    GetLinuxKernelVersion(major, minor, patch);
    info.mMajorVersion = major;
    info.mMinorVersion = minor;
    info.mSupportRIO = false;
    info.mSupportIOUring = IsLinuxIOUringSupported();
    info.mSupportKqueue = false;

#else
    info.mPlatformType = PlatformType::IOCP;
    info.mPlatformName = "Unknown";
#endif

    return info;
}

bool IsWindowsRIOSupported()
{
#ifdef _WIN32
    return IsWindows8OrGreater();
#else
    return false;
#endif
}

bool IsLinuxIOUringSupported()
{
#ifdef __linux__
    uint32_t major=0, minor=0, patch=0;
    if (!GetLinuxKernelVersion(major, minor, patch)) return false;
    if (major > 5) return true;
    if (major == 5 && minor >= 1) return true;
    return false;
#else
    return false;
#endif
}

bool IsLinuxEpollSupported()
{
#ifdef __linux__
    return true;
#else
    return false;
#endif
}

bool IsMacOSKqueueSupported()
{
#ifdef __APPLE__
    return true;
#else
    return false;
#endif
}

uint32_t GetWindowsMajorVersion()
{
#ifdef _WIN32
    if (IsWindows10OrGreater()) return 10;
    if (IsWindows8Point1OrGreater()) return 8;
    if (IsWindows8OrGreater()) return 8;
    if (IsWindows7OrGreater()) return 7;
    if (IsWindowsVistaOrGreater()) return 6;
    return 0;
#else
    return 0;
#endif
}

bool GetLinuxKernelVersion(uint32_t& outMajor, uint32_t& outMinor, uint32_t& outPatch)
{
#ifdef __linux__
    struct utsname buf{};
    if (uname(&buf) != 0) return false;
    int parsed = sscanf(buf.release, "%u.%u.%u", &outMajor, &outMinor, &outPatch);
    return parsed >= 2;
#else
    return false;
#endif
}

bool GetMacOSVersion(uint32_t& outMajor, uint32_t& outMinor, uint32_t& outPatch)
{
#ifdef __APPLE__
    int mib[2] = {CTL_KERN, KERN_OSRELEASE};
    char release[256] = {0};
    size_t len = sizeof(release);
    if (sysctl(mib, 2, release, &len, nullptr, 0) != 0) return false;
    int parsed = sscanf(release, "%u.%u.%u", &outMajor, &outMinor, &outPatch);
    return parsed >= 2;
#else
    return false;
#endif
}

} } }

