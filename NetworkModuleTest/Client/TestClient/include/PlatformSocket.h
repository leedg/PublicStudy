#pragma once

// English: Cross-platform socket abstraction for synchronous TCP client
// 한글: 동기 TCP 클라이언트용 크로스 플랫폼 소켓 추상화
//
// Provides platform-neutral types, constants, and inline helper functions
// so that TestClient code can avoid #ifdef blocks in business logic.

#ifdef _WIN32
// =============================================================================
// Windows (Winsock2)
// =============================================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>

// English: Socket handle type (Windows: SOCKET)
// 한글: 소켓 핸들 타입 (Windows: SOCKET)
using SocketHandle = SOCKET;

// English: Invalid socket constant
// 한글: 유효하지 않은 소켓 상수
constexpr SocketHandle INVALID_SOCKET_HANDLE = INVALID_SOCKET;

// English: Socket error return value
// 한글: 소켓 에러 반환값
constexpr int SOCKET_ERROR_VALUE = SOCKET_ERROR;

// English: Shutdown both directions constant
// 한글: 양방향 종료 상수
constexpr int SHUT_RDWR_VALUE = SD_BOTH;

// English: Get last socket error code
// 한글: 마지막 소켓 에러 코드 얻기
inline int PlatformGetLastError() { return WSAGetLastError(); }

// English: Check if error is timeout or would-block (not a real error)
// 한글: 에러가 타임아웃 또는 would-block인지 확인 (실제 에러 아님)
inline bool IsTimeoutOrWouldBlock(int err)
{
	return err == WSAETIMEDOUT || err == WSAEWOULDBLOCK;
}

// English: Close a socket
// 한글: 소켓 닫기
inline int PlatformCloseSocket(SocketHandle s) { return closesocket(s); }

// English: Initialize socket platform (Winsock startup)
// 한글: 소켓 플랫폼 초기화 (Winsock 시작)
inline bool PlatformSocketInit()
{
	WSADATA wsaData;
	return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
}

// English: Cleanup socket platform (Winsock cleanup)
// 한글: 소켓 플랫폼 정리 (Winsock 정리)
inline void PlatformSocketCleanup() { WSACleanup(); }

// English: Set receive timeout on socket (milliseconds)
// 한글: 소켓의 수신 타임아웃 설정 (밀리초)
inline void PlatformSetRecvTimeout(SocketHandle s, int timeoutMs)
{
	DWORD timeout = static_cast<DWORD>(timeoutMs);
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
			   reinterpret_cast<const char *>(&timeout), sizeof(timeout));
}

// English: Set TCP_NODELAY option on socket
// 한글: 소켓에 TCP_NODELAY 옵션 설정
inline void PlatformSetTcpNoDelay(SocketHandle s, bool enable)
{
	BOOL nodelay = enable ? TRUE : FALSE;
	setsockopt(s, IPPROTO_TCP, TCP_NODELAY,
			   reinterpret_cast<const char *>(&nodelay), sizeof(nodelay));
}

#else
// =============================================================================
// POSIX (Linux, macOS)
// =============================================================================
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>

// English: Socket handle type (POSIX: int file descriptor)
// 한글: 소켓 핸들 타입 (POSIX: int 파일 디스크립터)
using SocketHandle = int;

// English: Invalid socket constant
// 한글: 유효하지 않은 소켓 상수
constexpr SocketHandle INVALID_SOCKET_HANDLE = -1;

// English: Socket error return value
// 한글: 소켓 에러 반환값
constexpr int SOCKET_ERROR_VALUE = -1;

// English: Shutdown both directions constant
// 한글: 양방향 종료 상수
constexpr int SHUT_RDWR_VALUE = SHUT_RDWR;

// English: Get last socket error code
// 한글: 마지막 소켓 에러 코드 얻기
inline int PlatformGetLastError() { return errno; }

// English: Check if error is timeout or would-block (not a real error)
// 한글: 에러가 타임아웃 또는 would-block인지 확인 (실제 에러 아님)
inline bool IsTimeoutOrWouldBlock(int err)
{
	return err == EAGAIN || err == EWOULDBLOCK || err == ETIMEDOUT;
}

// English: Close a socket
// 한글: 소켓 닫기
inline int PlatformCloseSocket(SocketHandle s) { return close(s); }

// English: Initialize socket platform (POSIX: no-op)
// 한글: 소켓 플랫폼 초기화 (POSIX: 불필요)
inline bool PlatformSocketInit() { return true; }

// English: Cleanup socket platform (POSIX: no-op)
// 한글: 소켓 플랫폼 정리 (POSIX: 불필요)
inline void PlatformSocketCleanup() {}

// English: Set receive timeout on socket (milliseconds)
// 한글: 소켓의 수신 타임아웃 설정 (밀리초)
inline void PlatformSetRecvTimeout(SocketHandle s, int timeoutMs)
{
	struct timeval tv;
	tv.tv_sec = timeoutMs / 1000;
	tv.tv_usec = (timeoutMs % 1000) * 1000;
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
			   reinterpret_cast<const char *>(&tv), sizeof(tv));
}

// English: Set TCP_NODELAY option on socket
// 한글: 소켓에 TCP_NODELAY 옵션 설정
inline void PlatformSetTcpNoDelay(SocketHandle s, bool enable)
{
	int nodelay = enable ? 1 : 0;
	setsockopt(s, IPPROTO_TCP, TCP_NODELAY,
			   reinterpret_cast<const char *>(&nodelay), sizeof(nodelay));
}

#endif
