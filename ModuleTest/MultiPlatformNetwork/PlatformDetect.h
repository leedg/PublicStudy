#pragma once

// English: Platform detection utilities for AsyncIO backend selection
// ?쒓?: AsyncIO 諛깆뿏???좏깮???꾪븳 ?뚮옯??媛먯? ?좏떥由ы떚

#include "AsyncIOProvider.h"

namespace Network
{
namespace AsyncIO
{
namespace Platform
{
// =============================================================================
// English: Platform Detection Utilities
// ?쒓?: ?뚮옯??媛먯? ?좏떥由ы떚
// =============================================================================

/**
 * English: Detect the current platform at runtime
 * ?쒓?: ?고??꾩뿉 ?꾩옱 ?뚮옯??媛먯?
 * @return Detected PlatformType
 */
PlatformType DetectPlatform();

/**
 * English: Get detailed platform information
 * ?쒓?: ?곸꽭 ?뚮옯???뺣낫 議고쉶
 * @return PlatformInfo structure with version and capability information
 */
PlatformInfo GetDetailedPlatformInfo();

/**
 * English: Check if RIO (Registered I/O) is supported on Windows
 * ?쒓?: Windows?먯꽌 RIO (?깅줉 I/O) 吏???щ? ?뺤씤
 * @return true if Windows 8+ with RIO support
 */
bool IsWindowsRIOSupported();

/**
 * English: Check if io_uring is supported on Linux
 * ?쒓?: Linux?먯꽌 io_uring 吏???щ? ?뺤씤
 * @return true if Linux 5.1+ kernel with io_uring support
 */
bool IsLinuxIOUringSupported();

/**
 * English: Check if epoll is supported on Linux
 * ?쒓?: Linux?먯꽌 epoll 吏???щ? ?뺤씤
 * @return true if Linux with epoll support (almost all modern Linux)
 */
bool IsLinuxEpollSupported();

/**
 * English: Check if kqueue is supported on macOS
 * ?쒓?: macOS?먯꽌 kqueue 吏???щ? ?뺤씤
 * @return true if macOS (all versions support kqueue)
 */
bool IsMacOSKqueueSupported();

/**
 * English: Get Windows major version (e.g., 10 for Windows 10)
 * ?쒓?: Windows 二?踰꾩쟾 議고쉶 (?? Windows 10?대㈃ 10)
 * @return Windows major version, or 0 if not Windows
 */
uint32_t GetWindowsMajorVersion();

/**
 * English: Get Linux kernel version
 * ?쒓?: Linux 而ㅻ꼸 踰꾩쟾 議고쉶
 * @param outMajor Output: major version
 * @param outMinor Output: minor version
 * @param outPatch Output: patch version
 * @return true if version detected successfully
 */
bool GetLinuxKernelVersion(uint32_t &outMajor, uint32_t &outMinor,
							   uint32_t &outPatch);

/**
 * English: Get macOS version
 * ?쒓?: macOS 踰꾩쟾 議고쉶
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
