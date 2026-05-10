#pragma once

// English: Platform detection utilities — 6-Layer define hierarchy
// 한글: 플랫폼 감지 유틸리티 — 6계층 Define 계층 구조
//
// Layer 1: Native OS      — NATIVE_OS_WINDOWS, NATIVE_OS_LINUX, NATIVE_OS_MACOS
// Layer 2: Runtime Env    — DOCKER_CONTAINER (set by CMake)
// Layer 3: Platform Alias — IS_WINDOWS, IS_LINUX, IS_MACOS
// Layer 4: Net Capability — HAS_IOCP, HAS_RIO, HAS_EPOLL, HAS_IO_URING, HAS_KQUEUE
// Layer 5: DB Backend     — HAS_ODBC, HAS_OLEDB
// Layer 6: Utility        — ENDIAN_LITTLE, SOCKET_HANDLE, OS_ERROR

// =============================================================================
// AsyncIOProvider header (must come first — provides PlatformType, PlatformInfo)
// =============================================================================

// English: Prevent windows.h from defining max/min macros that conflict with std::max/min
// 한글: windows.h의 max/min 매크로가 std::max/min과 충돌하지 않도록 방지
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "AsyncIOProvider.h"

// =============================================================================
// Layer 1: Native OS Detection (compile-time, from compiler predefs)
// =============================================================================
#if defined(_WIN32)
    #define NATIVE_OS_WINDOWS
#elif defined(__linux__)
    #define NATIVE_OS_LINUX
#elif defined(__APPLE__)
    #define NATIVE_OS_MACOS
#endif



// =============================================================================
// Layer 2: Runtime Environment
// =============================================================================

// =============================================================================
// Layer 3: Platform Alias (readable names for #ifdef)
// =============================================================================
#if defined(NATIVE_OS_WINDOWS)
    #define IS_WINDOWS
#elif defined(NATIVE_OS_LINUX)
    #define IS_LINUX
#elif defined(NATIVE_OS_MACOS)
    #define IS_MACOS
#endif

// =============================================================================
// Layer 4: Network I/O Capability
// =============================================================================
#if defined(IS_WINDOWS)
    #define HAS_IOCP
    #define HAS_RIO
#endif

#if defined(IS_LINUX)
    #define HAS_EPOLL
    #if defined(HAVE_IO_URING) || defined(HAVE_LIBURING)
        #define HAS_IO_URING
    #endif
#endif

#if defined(IS_MACOS)
    #define HAS_KQUEUE
#endif

// =============================================================================
// Layer 5: Database Backend
// =============================================================================
#if defined(USE_OLEDB)
    #if defined(IS_WINDOWS)
        #define HAS_OLEDB
    #else
        #pragma message("WARNING: USE_OLEDB is only supported on Windows — falling back to ODBC")
        #define HAS_ODBC
    #endif
#else
    #define HAS_ODBC
#endif

// =============================================================================
// Legacy compatibility aliases (deprecated — migrate to new names)
// =============================================================================
#if defined(NATIVE_OS_WINDOWS)
    #define PLATFORM_WINDOWS
#endif
#if defined(NATIVE_OS_LINUX)
    #define PLATFORM_LINUX
#endif
#if defined(NATIVE_OS_MACOS)
    #define PLATFORM_MACOS
#endif

namespace Network
{
namespace AsyncIO
{
namespace Platform
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
 * English: Check if current build is running inside a Docker container
 * 한글: 현재 빌드가 Docker 컨테이너 내부에서 실행 중인지 확인
 * @return true if DOCKER_CONTAINER is defined
 */
inline bool IsDockerContainer()
{
#ifdef DOCKER_CONTAINER
    return true;
#else
    return false;
#endif
}

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
bool GetLinuxKernelVersion(uint32_t &outMajor, uint32_t &outMinor,
                               uint32_t &outPatch);

/**
 * English: Get macOS version
 * 한글: macOS 버전 조회
 * @param outMajor Output: major version
 * @param outMinor Output: minor version
 * @param outPatch Output: patch version
 * @return true if version detected successfully
 */
bool GetMacOSVersion(uint32_t &outMajor, uint32_t &outMinor,
                     uint32_t &outPatch);

} // namespace Platform
} // namespace AsyncIO
} // namespace Network
