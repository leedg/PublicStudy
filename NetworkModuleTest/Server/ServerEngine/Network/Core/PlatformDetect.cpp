// English: Platform detection implementation
// 한글: 플랫폼 감지 구현

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

namespace Network::AsyncIO::Platform
{
    // =============================================================================
    // English: Platform Detection Implementation
    // 한글: 플랫폼 감지 구현
    // =============================================================================

    PlatformType DetectPlatform()
    {
        // English: Return default backend for current OS
        // 한글: 현재 OS의 기본 백엔드 반환
#ifdef _WIN32
        return PlatformType::IOCP;    // English: Windows default / 한글: Windows 기본
#elif __APPLE__
        return PlatformType::Kqueue;  // English: macOS default / 한글: macOS 기본
#elif __linux__
        return PlatformType::Epoll;   // English: Linux default / 한글: Linux 기본
#else
        return PlatformType::IOCP;    // English: Fallback / 한글: 폴백
#endif
    }

    PlatformInfo GetDetailedPlatformInfo()
    {
        // English: Build detailed platform info structure
        // 한글: 상세 플랫폼 정보 구조체 구성
        PlatformInfo info{};

#ifdef _WIN32
        // English: Windows Platform
        // 한글: Windows 플랫폼
        info.mPlatformType = PlatformType::IOCP;
        info.mPlatformName = "Windows";

        // English: Detect Windows version
        // 한글: Windows 버전 감지
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

        // English: Check RIO support (Windows 8+)
        // 한글: RIO 지원 확인 (Windows 8+)
        info.mSupportRIO = IsWindows8OrGreater();
        info.mSupportIOUring = false;
        info.mSupportKqueue = false;

#elif __APPLE__
        // English: macOS Platform
        // 한글: macOS 플랫폼
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
        info.mSupportKqueue = true;  // English: Always supported / 한글: 항상 지원

#elif __linux__
        // English: Linux Platform
        // 한글: Linux 플랫폼
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
        // English: Unknown platform
        // 한글: 알 수 없는 플랫폼
        info.mPlatformType = PlatformType::IOCP;
        info.mPlatformName = "Unknown";
#endif

        return info;
    }

    bool IsWindowsRIOSupported()
    {
        // English: RIO is supported on Windows 8 and later
        // 한글: RIO는 Windows 8 이상에서 지원
#ifdef _WIN32
        return IsWindows8OrGreater();
#else
        return false;
#endif
    }

    bool IsLinuxIOUringSupported()
    {
        // English: io_uring requires Linux kernel 5.1+
        // 한글: io_uring은 Linux 커널 5.1+ 필요
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
        // English: epoll is supported on virtually all modern Linux
        // 한글: epoll은 거의 모든 최신 Linux에서 지원
#ifdef __linux__
        return true;
#else
        return false;
#endif
    }

    bool IsMacOSKqueueSupported()
    {
        // English: kqueue is supported on all macOS versions
        // 한글: kqueue는 모든 macOS 버전에서 지원
#ifdef __APPLE__
        return true;
#else
        return false;
#endif
    }

    uint32_t GetWindowsMajorVersion()
    {
        // English: Return Windows major version number
        // 한글: Windows 주 버전 번호 반환
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
        // English: Parse Linux kernel version from uname
        // 한글: uname에서 Linux 커널 버전 파싱
#ifdef __linux__
        struct utsname buf{};
        if (uname(&buf) != 0)
            return false;

        // English: Parse version string like "5.10.0-8-generic"
        // 한글: "5.10.0-8-generic" 같은 버전 문자열 파싱
        int parsed = sscanf(buf.release, "%u.%u.%u", &outMajor, &outMinor, &outPatch);
        return parsed >= 2;  // English: At least major.minor required / 한글: 최소 major.minor 필요
#else
        return false;
#endif
    }

    bool GetMacOSVersion(uint32_t& outMajor, uint32_t& outMinor, uint32_t& outPatch)
    {
        // English: Get macOS version via sysctl
        // 한글: sysctl을 통한 macOS 버전 조회
#ifdef __APPLE__
        int mib[2] = {CTL_KERN, KERN_OSRELEASE};
        char release[256] = {0};
        size_t len = sizeof(release);

        if (sysctl(mib, 2, release, &len, nullptr, 0) != 0)
            return false;

        // English: Parse version string like "20.6.0"
        // 한글: "20.6.0" 같은 버전 문자열 파싱
        int parsed = sscanf(release, "%u.%u.%u", &outMajor, &outMinor, &outPatch);
        return parsed >= 2;
#else
        return false;
#endif
    }

}  // namespace Network::AsyncIO::Platform
