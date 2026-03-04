#pragma once

// English: macOS-specific NetworkEngine implementation
// 한글: macOS 전용 NetworkEngine 구현
//
// Uses kqueue for high-performance event notification
// kqueue를 사용한 고성능 이벤트 알림

#ifdef __APPLE__

#include "../Core/BaseNetworkEngine.h"
#include <thread>
#include <vector>

namespace Network::Platforms
{

// =============================================================================
// English: macOS NetworkEngine
// 한글: macOS NetworkEngine
// =============================================================================

class macOSNetworkEngine : public Core::BaseNetworkEngine
{
  public:
	/**
	 * English: Constructor
	 * 한글: 생성자
	 */
	explicit macOSNetworkEngine();
	virtual ~macOSNetworkEngine();

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

	// English: Queue recv for a session
	// 한글: 세션 수신 등록
	bool QueueRecv(const Core::SessionRef &session);

	// English: Worker thread function
	// 한글: 워커 스레드 함수
	void WorkerThread();

  private:
	// English: Listen socket
	// 한글: Listen 소켓
	int mListenSocket;

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

#endif // __APPLE__
