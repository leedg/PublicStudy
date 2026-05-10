// English: Platform detection implementation — updated to use 6-layer define hierarchy
// 한글: 플랫폼 감지 구현 — 6계층 define 계층 구조로 업데이트

#include "PlatformDetect.h"
#include <cstdio>
#include <cstring>

#if defined(IS_WINDOWS)
#include <versionhelpers.h>
#include <windows.h>
#elif defined(IS_MACOS)
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

PlatformType DetectPlatform()
{
#if defined(IS_WINDOWS)
    return PlatformType::IOCP;
#elif defined(IS_MACOS)
    return PlatformType::Kqueue;
#elif defined(IS_LINUX)
    return PlatformType::Epoll;
#else
    return PlatformType::IOCP;
#endif
}

PlatformInfo GetDetailedPlatformInfo()
{
    PlatformInfo info{};

#if defined(IS_WINDOWS)
    info.mPlatformType = PlatformType::IOCP;
    info.mPlatformName = "Windows";

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
    info.mSupportRIO = IsWindows8OrGreater();
    info.mSupportIOUring = false;
    info.mSupportKqueue = false;

#elif defined(IS_MACOS)
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
    info.mSupportKqueue = true;

#elif defined(IS_LINUX)
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
    info.mPlatformType = PlatformType::IOCP;
    info.mPlatformName = "Unknown";
#endif

    return info;
}

bool IsWindowsRIOSupported()
{
#if defined(IS_WINDOWS)
    return IsWindows8OrGreater();
#else
    return false;
#endif
}

bool IsLinuxIOUringSupported()
{
#if defined(IS_LINUX)
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
#if defined(IS_LINUX)
    return true;
#else
    return false;
#endif
}

bool IsMacOSKqueueSupported()
{
#if defined(IS_MACOS)
    return true;
#else
    return false;
#endif
}

uint32_t GetWindowsMajorVersion()
{
#if defined(IS_WINDOWS)
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
#if defined(IS_LINUX)
    struct utsname buf{};
    if (uname(&buf) != 0)
        return false;

    int parsed =
        sscanf(buf.release, "%u.%u.%u", &outMajor, &outMinor, &outPatch);
    return parsed >= 2;
#else
    return false;
#endif
}

bool GetMacOSVersion(uint32_t &outMajor, uint32_t &outMinor, uint32_t &outPatch)
{
#if defined(IS_MACOS)
    int mib[2] = {CTL_KERN, KERN_OSRELEASE};
    char release[256] = {0};
    size_t len = sizeof(release);

    if (sysctl(mib, 2, release, &len, nullptr, 0) != 0)
        return false;

    int parsed = sscanf(release, "%u.%u.%u", &outMajor, &outMinor, &outPatch);
    return parsed >= 2;
#else
    return false;
#endif
}

} // namespace Platform
} // namespace AsyncIO
} // namespace Network
