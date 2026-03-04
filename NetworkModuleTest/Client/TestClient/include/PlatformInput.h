#pragma once

// English: Cross-platform terminal keyboard input abstraction
// 한글: 크로스 플랫폼 터미널 키보드 입력 추상화
//
// NOTE: This header should only be included from a single translation unit
// (main.cpp) due to the static variables in the POSIX anonymous namespace.

// =============================================================================
// English: Platform-specific keyboard input headers
// 한글: 플랫폼별 키보드 입력 헤더
// =============================================================================
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <conio.h>
#include <windows.h>
#else
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#include <cstdlib>
#endif

// =============================================================================
// English: POSIX keyboard input helpers (Linux/macOS)
// 한글: POSIX 키보드 입력 헬퍼 (Linux/macOS)
// =============================================================================
#ifndef _WIN32
namespace
{

// English: Original terminal attributes for restoration on exit
// 한글: 종료 시 복원을 위한 원래 터미널 속성
static struct termios g_OrigTermios;
static bool g_TermiosModified = false;

// English: Restore terminal to original mode (called on exit)
// 한글: 터미널을 원래 모드로 복원 (종료 시 호출)
inline void RestoreTerminal()
{
	if (g_TermiosModified)
	{
		tcsetattr(STDIN_FILENO, TCSANOW, &g_OrigTermios);
		g_TermiosModified = false;
	}
}

// English: Enable raw mode for single-character input without echo
// 한글: 에코 없이 단일 문자 입력을 위한 raw 모드 활성화
inline void EnableRawMode()
{
	if (g_TermiosModified)
	{
		return; // English: Already in raw mode / 한글: 이미 raw 모드
	}

	tcgetattr(STDIN_FILENO, &g_OrigTermios);
	g_TermiosModified = true;

	// English: Register atexit handler to restore terminal on abnormal exit
	// 한글: 비정상 종료 시 터미널 복원을 위한 atexit 핸들러 등록
	std::atexit(RestoreTerminal);

	struct termios raw = g_OrigTermios;
	// English: Disable canonical mode and echo
	// 한글: 정규 모드와 에코 비활성화
	raw.c_lflag &= ~(ICANON | ECHO);
	// English: Read returns immediately with available bytes (non-blocking feel)
	// 한글: 읽기가 사용 가능한 바이트로 즉시 반환 (논블로킹 느낌)
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 0;
	tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

// English: Check if keyboard input is available (POSIX equivalent of _kbhit)
// 한글: 키보드 입력이 있는지 확인 (POSIX용 _kbhit 대체)
inline bool PosixKbhit()
{
	struct timeval tv = {0, 0};
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(STDIN_FILENO, &fds);
	return select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0;
}

// English: Read a single character without echo (POSIX equivalent of _getch)
// 한글: 에코 없이 단일 문자 읽기 (POSIX용 _getch 대체)
inline char PosixGetch()
{
	char ch = 0;
	if (read(STDIN_FILENO, &ch, 1) == 1)
	{
		return ch;
	}
	return 0;
}

} // anonymous namespace
#endif // !_WIN32

// =============================================================================
// English: Unified cross-platform keyboard input functions
// 한글: 통합 크로스 플랫폼 키보드 입력 함수
// =============================================================================
#ifdef _WIN32
inline bool HasKeyInput() { return _kbhit() != 0; }
inline char ReadKeyChar() { return static_cast<char>(_getch()); }
#else
inline bool HasKeyInput() { return PosixKbhit(); }
inline char ReadKeyChar() { return PosixGetch(); }
#endif
