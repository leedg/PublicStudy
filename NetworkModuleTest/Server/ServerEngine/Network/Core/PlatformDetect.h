#pragma once

// Platform detection utilities — AsyncIO backend selection + DB backend macros.

// =============================================================================
// Compile-time platform macros
//
//   Network I/O backend (auto-selected; can be overridden by predefining the macro):
//     PLATFORM_WINDOWS   — defined on _WIN32
//     PLATFORM_LINUX     — defined on __linux__
//     PLATFORM_MACOS     — defined on __APPLE__
//
//   Database backend (select at build time by defining one of these before including):
//     DB_BACKEND_ODBC    — use ODBC driver (default on all platforms)
//     DB_BACKEND_OLEDB   — use OLE DB driver (Windows only; define USE_OLEDB to activate)
//
//   Usage examples:
//     // Force OLEDB on Windows (define in project settings or before #include):
//     #define USE_OLEDB
//     #include "PlatformDetect.h"
//
//     // Select backend at compile time:
//     #if defined(DB_BACKEND_OLEDB)
//         // OLE DB code path
//     #elif defined(DB_BACKEND_ODBC)
//         // ODBC code path
//     #endif
//
//
//
//
//     #define USE_OLEDB
//     #include "PlatformDetect.h"
//
//     #if defined(DB_BACKEND_OLEDB)
//     #elif defined(DB_BACKEND_ODBC)
//     #endif
// =============================================================================

// ── Network I/O platform macros ──────────────────────────────────────────────
#if defined(_WIN32)
    #define PLATFORM_WINDOWS
#elif defined(__linux__)
    #define PLATFORM_LINUX
#elif defined(__APPLE__)
    #define PLATFORM_MACOS
#endif

// ── Database backend macros ──────────────────────────────────────────────────
//
// OLE DB is Windows-only. If USE_OLEDB is defined on a non-Windows
//          platform, we silently fall back to ODBC with a compile-time warning.
#if defined(USE_OLEDB)
    #if defined(PLATFORM_WINDOWS)
        #define DB_BACKEND_OLEDB
    #else
        #pragma message("WARNING: USE_OLEDB is only supported on Windows — falling back to ODBC")
        #define DB_BACKEND_ODBC
    #endif
#else
    #define DB_BACKEND_ODBC  // default on all platforms
#endif

// ── AsyncIOProvider header (must come after macro definitions) ────────────────
#include "AsyncIOProvider.h"

namespace Network
{
namespace AsyncIO
{
namespace Platform
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
 * @param outMajor Output: major version
 * @param outMinor Output: minor version
 * @param outPatch Output: patch version
 * @return true if version detected successfully
 */
bool GetLinuxKernelVersion(uint32_t &outMajor, uint32_t &outMinor,
							   uint32_t &outPatch);

/**
 * Get macOS version
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
