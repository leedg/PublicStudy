#pragma once

#include "AsyncIOProvider.h"

namespace Network::AsyncIO::Platform
{
    // =============================================================================
    // Platform Detection Utilities
    // =============================================================================

    /**
     * Detect the current platform at runtime
     * @return Detected PlatformType
     */
    PlatformType DetectPlatform();

    /**
     * Get detailed platform information
     * @return PlatformInfo structure with version and capability information
     */
    PlatformInfo GetDetailedPlatformInfo();

    /**
     * Check if RIO (Registered I/O) is supported on Windows
     * @return true if Windows 8+ with RIO support
     */
    bool IsWindowsRIOSupported();

    /**
     * Check if io_uring is supported on Linux
     * @return true if Linux 5.1+ kernel with io_uring support
     */
    bool IsLinuxIOUringSupported();

    /**
     * Check if epoll is supported on Linux
     * @return true if Linux with epoll support (almost all modern Linux)
     */
    bool IsLinuxEpollSupported();

    /**
     * Check if kqueue is supported on macOS
     * @return true if macOS (all versions support kqueue)
     */
    bool IsMacOSKqueueSupported();

    /**
     * Get Windows major version (e.g., 10 for Windows 10)
     * @return Windows major version, or 0 if not Windows
     */
    uint32_t GetWindowsMajorVersion();

    /**
     * Get Linux kernel version
     * @param outMajor Output parameter for major version
     * @param outMinor Output parameter for minor version
     * @param outPatch Output parameter for patch version
     * @return true if version detected successfully
     */
    bool GetLinuxKernelVersion(uint32_t& outMajor, uint32_t& outMinor, uint32_t& outPatch);

    /**
     * Get macOS version
     * @param outMajor Output parameter for major version
     * @param outMinor Output parameter for minor version
     * @param outPatch Output parameter for patch version
     * @return true if version detected successfully
     */
    bool GetMacOSVersion(uint32_t& outMajor, uint32_t& outMinor, uint32_t& outPatch);

}  // namespace Network::AsyncIO::Platform
