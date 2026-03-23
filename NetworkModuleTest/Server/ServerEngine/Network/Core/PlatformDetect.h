#pragma once

// 플랫폼 감지 유틸리티 — AsyncIO 백엔드 선택 + DB 백엔드 매크로.
//
// =============================================================================
// 컴파일 타임 플랫폼 매크로
//
//   네트워크 I/O 백엔드 (자동 선택):
//     PLATFORM_WINDOWS   — _WIN32에서 정의됨
//     PLATFORM_LINUX     — __linux__에서 정의됨
//     PLATFORM_MACOS     — __APPLE__에서 정의됨
//
//   데이터베이스 백엔드 (빌드 시 선택):
//     DB_BACKEND_ODBC    — ODBC 드라이버 사용 (모든 플랫폼 기본값)
//     DB_BACKEND_OLEDB   — OLE DB 드라이버 사용 (Windows 전용; USE_OLEDB 정의 시 활성화)
//
//   사용 예시:
//     #define USE_OLEDB        // Windows에서 OLEDB 강제 사용
//     #include "PlatformDetect.h"
//
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
// OLE DB는 Windows 전용. 비-Windows에서 USE_OLEDB 정의 시 ODBC로 폴백하고
// 컴파일 타임 경고를 발생시킨다.
#if defined(USE_OLEDB)
    #if defined(PLATFORM_WINDOWS)
        #define DB_BACKEND_OLEDB
    #else
        #pragma message("WARNING: USE_OLEDB is only supported on Windows — falling back to ODBC")
        #define DB_BACKEND_ODBC
    #endif
#else
    #define DB_BACKEND_ODBC  // 모든 플랫폼 기본값
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
// 플랫폼 감지 유틸리티
// =============================================================================

/** 런타임에 현재 플랫폼 타입 반환 */
PlatformType DetectPlatform();

/** 버전 및 기능 정보를 포함한 상세 플랫폼 정보 조회 */
PlatformInfo GetDetailedPlatformInfo();

/** Windows RIO 지원 여부 확인 (Windows 8+ 기준) */
bool IsWindowsRIOSupported();

/** Linux io_uring 지원 여부 확인 (커널 5.1+ 기준) */
bool IsLinuxIOUringSupported();

/** Linux epoll 지원 여부 확인 (거의 모든 현대 Linux에서 true) */
bool IsLinuxEpollSupported();

/** macOS kqueue 지원 여부 확인 (모든 macOS에서 true) */
bool IsMacOSKqueueSupported();

/** Windows 주 버전 반환 (Windows가 아니면 0) */
uint32_t GetWindowsMajorVersion();

/**
 * Linux 커널 버전 파싱 (uname 기반).
 * @return 버전 감지 성공 여부
 */
bool GetLinuxKernelVersion(uint32_t &outMajor, uint32_t &outMinor,
							   uint32_t &outPatch);

/**
 * macOS 버전 조회 (sysctl 기반).
 * @return 버전 감지 성공 여부
 */
bool GetMacOSVersion(uint32_t &outMajor, uint32_t &outMinor,
					 uint32_t &outPatch);

} // namespace Platform
} // namespace AsyncIO
} // namespace Network
