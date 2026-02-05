#pragma once

// English: Linux-specific NetworkEngine implementation
// 한글: Linux 전용 NetworkEngine 구현
//
// Supports two modes:
// - epoll: Standard event notification (all Linux versions)
// - io_uring: Modern async I/O (Linux 5.1+, high performance)
//
// 두 가지 모드 지원:
// - epoll: 표준 이벤트 알림 (모든 Linux 버전)
// - io_uring: 최신 비동기 I/O (Linux 5.1+, 고성능)

#ifdef __linux__

#include "../Core/BaseNetworkEngine.h"
#include <thread>
#include <vector>

namespace Network::Platforms
{

// =============================================================================
// English: Linux NetworkEngine
// 한글: Linux NetworkEngine
// =============================================================================

class LinuxNetworkEngine : public Core::BaseNetworkEngine
{
  public:
	// English: I/O backend mode
	// 한글: I/O 백엔드 모드
	enum class Mode
	{
		Epoll,    // English: Standard epoll / 한글: 표준 epoll
		IOUring   // English: io_uring / 한글: io_uring
	};

	/**
	 * English: Constructor
	 * 한글: 생성자
	 * @param mode I/O backend mode (Epoll or IOUring)
	 */
	explicit LinuxNetworkEngine(Mode mode = Mode::Epoll);
	virtual ~LinuxNetworkEngine();

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
	int mListenSocket;

	// English: Accept thread
	// 한글: Accept 스레드
	std::thread mAcceptThread;

	// English: Worker threads (for completion processing)
	// 한글: 워커 스레드 (완료 처리용)
	std::vector<std::thread> mWorkerThreads;
};

} // namespace Network::Platforms

#endif // __linux__
