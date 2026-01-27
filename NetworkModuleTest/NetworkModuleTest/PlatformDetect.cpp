#include "PlatformDetect.h"
#include <cstring>

#ifdef _WIN32
    #include <windows.h>
    #include <versionhelpers.h>
#elif __APPLE__
    #include <TargetConditionals.h>
    #include <sys/sysctl.h>
#else  // Linux
    #include <sys/utsname.h>
#endif

namespace RAON::Network::AsyncIO::Platform
{
    // =============================================================================
    // Platform Detection Implementation
    // =============================================================================

    PlatformType DetectPlatform()
    {
#ifdef _WIN32
        return PlatformType::IOCP;  // Default to IOCP, RIO will be checked separately
#elif __APPLE__
        return PlatformType::Kqueue;
#elif __linux__
        return PlatformType::Epoll;  // Default to epoll, io_uring will be checked separately
#else
        return PlatformType::IOCP;  // Fallback
#endif
    }

    PlatformInfo GetDetailedPlatformInfo()
    {
        PlatformInfo info{};
        
#ifdef _WIN32
        // Windows Platform
        info.platformType = PlatformType::IOCP;
        info.platformName = "Windows";
        
        // Get Windows version
        uint32_t major = 0, minor = 0;
        
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
            minor = 0;
        }
        else if (IsWindows7OrGreater())
        {
            major = 7;
        }
        else if (IsWindowsVistaOrGreater())
        {
            major = 6;
            minor = 0;
        }
        else
        {
            major = 6;
        }
        
        info.majorVersion = major;
        info.minorVersion = minor;
        
        // Check RIO support (Windows 8+)
        info.supportRIO = IsWindows8OrGreater();
        info.supportIOUring = false;
        info.supportKqueue = false;

#elif __APPLE__
        // macOS Platform
        info.platformType = PlatformType::Kqueue;
        info.platformName = "macOS";
        
        // Get macOS version
        uint32_t major = 0, minor = 0, patch = 0;
        GetMacOSVersion(major, minor, patch);
        
        info.majorVersion = major;
        info.minorVersion = minor;
        
        info.supportRIO = false;
        info.supportIOUring = false;
        info.supportKqueue = true;  // kqueue always supported on macOS

#elif __linux__
        // Linux Platform
        info.platformType = PlatformType::Epoll;
        info.platformName = "Linux";
        
        // Get Linux kernel version
        uint32_t major = 0, minor = 0, patch = 0;
        GetLinuxKernelVersion(major, minor, patch);
        
        info.majorVersion = major;
        info.minorVersion = minor;
        
        info.supportRIO = false;
        info.supportIOUring = IsLinuxIOUringSupported();
        info.supportKqueue = false;

#else
        info.platformType = PlatformType::IOCP;
        info.platformName = "Unknown";
        info.supportRIO = false;
        info.supportIOUring = false;
        info.supportKqueue = false;
#endif

        return info;
    }

    bool IsWindowsRIOSupported()
    {
#ifdef _WIN32
        // RIO is supported on Windows 8 and later
        return IsWindows8OrGreater();
#else
        return false;
#endif
    }

    bool IsLinuxIOUringSupported()
    {
#ifdef __linux__
        uint32_t major = 0, minor = 0, patch = 0;
        if (!GetLinuxKernelVersion(major, minor, patch))
            return false;
        
        // io_uring requires Linux 5.1+
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
#ifdef __linux__
        // epoll is supported on virtually all modern Linux systems
        return true;
#else
        return false;
#endif
    }

    bool IsMacOSKqueueSupported()
    {
#ifdef __APPLE__
        // kqueue is supported on all macOS versions
        return true;
#else
        return false;
#endif
    }

    uint32_t GetWindowsMajorVersion()
    {
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

    bool GetLinuxKernelVersion(uint32_t& outMajor, uint32_t& outMinor, uint32_t& outPatch)
    {
#ifdef __linux__
        struct utsname buf{};
        if (uname(&buf) != 0)
            return false;
        
        // Parse version string like "5.10.0-8-generic"
        int parsed = sscanf(buf.release, "%u.%u.%u", &outMajor, &outMinor, &outPatch);
        return parsed >= 2;  // At least major.minor required
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
        
        if (sysctl(mib, 2, release, &len, nullptr, 0) != 0)
            return false;
        
        // Parse version string like "20.6.0"
        int parsed = sscanf(release, "%u.%u.%u", &outMajor, &outMinor, &outPatch);
        return parsed >= 2;
#else
        return false;
#endif
    }

}  // namespace RAON::Network::AsyncIO::Platform
