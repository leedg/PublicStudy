#pragma once

// English: Windows-specific NetworkEngine implementation
// 한글: Windows 전용 NetworkEngine 구현
//
// Supports two modes:
// - IOCP: Standard I/O Completion Port (all Windows versions)
// - RIO: Registered I/O (Windows 8+, high performance)
//
// 두 가지 모드 지원:
// - IOCP: 표준 I/O 완료 포트 (모든 Windows 버전)
// - RIO: 등록 I/O (Windows 8+, 고성능)

#ifdef _WIN32

#include "../Core/BaseNetworkEngine.h"
#include <thread>
#include <vector>

namespace Network::Platforms
{

// =============================================================================
// English: Windows NetworkEngine
// 한글: Windows NetworkEngine
// =============================================================================

class WindowsNetworkEngine : public Core::BaseNetworkEngine
{
  public:
	// English: I/O backend mode
	// 한글: I/O 백엔드 모드
	enum class Mode
	{
		IOCP, // English: Standard IOCP / 한글: 표준 IOCP
		RIO   // English: Registered I/O / 한글: 등록 I/O
	};

	/**
	 * English: Constructor
	 * 한글: 생성자
	 * @param mode I/O backend mode (IOCP or RIO)
	 */
	explicit WindowsNetworkEngine(Mode mode = Mode::IOCP);
	virtual ~WindowsNetworkEngine();

  protected:
	// =====================================================================
	// English: Platform-specific implementation
	// 한글: 플랫폼별 구현
	// =====================================================================

	bool InitializePlatform() override;
	void ShutdownPlatform() override;
	bool StartPlatformIO() override;
	void StopPlatformIO() override;
	void AcceptLoop() override;
	void ProcessCompletions() override;

  private:
	// English: Initialize Winsock
	// 한글: Winsock 초기화
	bool InitializeWinsock();

	// English: Create listen socket
	// 한글: Listen 소켓 생성
	bool CreateListenSocket();

	// English: Worker thread function
	// 한글: 워커 스레드 함수
	void WorkerThread();

  private:
	// English: I/O mode
	// 한글: I/O 모드
	Mode mMode;

	// English: Listen socket
	// 한글: Listen 소켓
	SOCKET mListenSocket;

	// English: Accept loop backoff (ms) - member to avoid static variable bug
	// 한글: Accept 루프 백오프 (ms) - static 변수 버그 방지를 위한 멤버 변수
	int mAcceptBackoffMs;

	// English: Accept thread
	// 한글: Accept 스레드
	std::thread mAcceptThread;

	// English: Worker threads (for completion processing)
	// 한글: 워커 스레드 (완료 처리용)
	std::vector<std::thread> mWorkerThreads;
};

} // namespace Network::Platforms

#endif // _WIN32
