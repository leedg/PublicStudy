#pragma once

// English: Platform detection utilities for AsyncIO backend selection
// 한글: AsyncIO 백엔드 선택을 위한 플랫폼 감지 유틸리티

#include "AsyncIOProvider.h"

namespace Network::AsyncIO::Platform
{
    // =============================================================================
    // English: Platform Detection Utilities
    // 한글: 플랫폼 감지 유틸리티
    // =============================================================================

    /**
     * English: Detect the current platform at runtime
     * 한글: 런타임에 현재 플랫폼 감지
     * @return Detected PlatformType
     */
    PlatformType DetectPlatform();

    /**
     * English: Get detailed platform information
     * 한글: 상세 플랫폼 정보 조회
     * @return PlatformInfo structure with version and capability information
     */
    PlatformInfo GetDetailedPlatformInfo();

    /**
     * English: Check if RIO (Registered I/O) is supported on Windows
     * 한글: Windows에서 RIO (등록 I/O) 지원 여부 확인
     * @return true if Windows 8+ with RIO support
     */
    bool IsWindowsRIOSupported();

    /**
     * English: Check if io_uring is supported on Linux
     * 한글: Linux에서 io_uring 지원 여부 확인
     * @return true if Linux 5.1+ kernel with io_uring support
     */
    bool IsLinuxIOUringSupported();

    /**
     * English: Check if epoll is supported on Linux
     * 한글: Linux에서 epoll 지원 여부 확인
     * @return true if Linux with epoll support (almost all modern Linux)
     */
    bool IsLinuxEpollSupported();

    /**
     * English: Check if kqueue is supported on macOS
     * 한글: macOS에서 kqueue 지원 여부 확인
     * @return true if macOS (all versions support kqueue)
     */
    bool IsMacOSKqueueSupported();

    /**
     * English: Get Windows major version (e.g., 10 for Windows 10)
     * 한글: Windows 주 버전 조회 (예: Windows 10이면 10)
     * @return Windows major version, or 0 if not Windows
     */
    uint32_t GetWindowsMajorVersion();

    /**
     * English: Get Linux kernel version
     * 한글: Linux 커널 버전 조회
     * @param outMajor Output: major version
     * @param outMinor Output: minor version
     * @param outPatch Output: patch version
     * @return true if version detected successfully
     */
    bool GetLinuxKernelVersion(uint32_t& outMajor, uint32_t& outMinor, uint32_t& outPatch);

    /**
     * English: Get macOS version
     * 한글: macOS 버전 조회
     * @param outMajor Output: major version
     * @param outMinor Output: minor version
     * @param outPatch Output: patch version
     * @return true if version detected successfully
     */
    bool GetMacOSVersion(uint32_t& outMajor, uint32_t& outMinor, uint32_t& outPatch);

}  // namespace Network::AsyncIO::Platform
