#pragma once

// English: Platform detection utilities — AsyncIO backend selection + DB backend macros.
// 한글: 플랫폼 감지 유틸리티 — AsyncIO 백엔드 선택 + DB 백엔드 매크로.

// =============================================================================
// English: Compile-time platform macros
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
// 한글: 컴파일 타임 플랫폼 매크로
//
//   네트워크 I/O 백엔드 (자동 선택; 매크로 사전 정의로 오버라이드 가능):
//     PLATFORM_WINDOWS   — _WIN32에서 정의됨
//     PLATFORM_LINUX     — __linux__에서 정의됨
//     PLATFORM_MACOS     — __APPLE__에서 정의됨
//
//   데이터베이스 백엔드 (빌드 시 둘 중 하나를 정의하여 선택):
//     DB_BACKEND_ODBC    — ODBC 드라이버 사용 (모든 플랫폼 기본값)
//     DB_BACKEND_OLEDB   — OLE DB 드라이버 사용 (Windows 전용; USE_OLEDB 정의 시 활성화)
//
//   사용 예시:
//     // Windows에서 OLEDB 강제 사용 (프로젝트 설정 또는 include 전에 정의):
//     #define USE_OLEDB
//     #include "PlatformDetect.h"
//
//     // 컴파일 타임 백엔드 분기:
//     #if defined(DB_BACKEND_OLEDB)
//         // OLE DB 코드 경로
//     #elif defined(DB_BACKEND_ODBC)
//         // ODBC 코드 경로
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
// English: OLE DB is Windows-only. If USE_OLEDB is defined on a non-Windows
//          platform, we silently fall back to ODBC with a compile-time warning.
// 한글: OLE DB는 Windows 전용. 비-Windows에서 USE_OLEDB 정의 시 ODBC로 폴백하고
//       컴파일 타임 경고 발생.
#if defined(USE_OLEDB)
    #if defined(PLATFORM_WINDOWS)
        #define DB_BACKEND_OLEDB
    #else
        #pragma message("WARNING: USE_OLEDB is only supported on Windows — falling back to ODBC")
        #define DB_BACKEND_ODBC
    #endif
#else
    #define DB_BACKEND_ODBC  // English: default on all platforms / 한글: 모든 플랫폼 기본값
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
