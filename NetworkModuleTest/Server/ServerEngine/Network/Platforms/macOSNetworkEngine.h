#pragma once

// macOS 전용 NetworkEngine 구현.
//
// kqueue를 I/O 이벤트 알림 백엔드로 사용한다.
// kqueue는 macOS / FreeBSD 전용이며 epoll에 해당하는 BSD 계열 이벤트 인터페이스다.
// Linux의 MSG_NOSIGNAL이 macOS에 존재하지 않으므로, accept 직후 각 클라이언트
// 소켓에 SO_NOSIGPIPE를 설정하여 원격 종료 시 SIGPIPE 대신 EPIPE가 반환되도록 한다.

#ifdef __APPLE__

#include "../Core/BaseNetworkEngine.h"
#include <thread>
#include <vector>

namespace Network::Platforms
{

class macOSNetworkEngine : public Core::BaseNetworkEngine
{
  public:
	explicit macOSNetworkEngine();
	virtual ~macOSNetworkEngine();

  protected:
	// 플랫폼별 구현 (BaseNetworkEngine 순수 가상 재정의)
	bool InitializePlatform() override;
	void ShutdownPlatform() override;
	bool StartPlatformIO() override;
	void StopPlatformIO() override;
	void AcceptLoop() override;
	void ProcessCompletions() override;

  private:
	// listen 소켓 생성 및 바인드 (논블로킹 모드, SO_REUSEADDR 설정 포함)
	bool CreateListenSocket();

	// 세션에 recv 작업 등록 (kqueue EVFILT_READ 방식)
	bool QueueRecv(const Core::SessionRef &session);

	// 완료 처리 루프 (WorkerThread 내부에서 반복 호출)
	void WorkerThread();

  private:
	// ─────────────────────────────────────────────
	// 소켓
	// ─────────────────────────────────────────────
	int mListenSocket;    // TCP listen fd — POSIX fd; -1 = 미초기화

	// Accept 루프에서 연속 오류 발생 시 지수 백오프용 대기 시간(ms).
	// static 변수 대신 멤버 변수로 유지하여 재진입/복수 인스턴스 버그를 방지한다.
	int mAcceptBackoffMs; // 초기값 10ms, 오류 지속 시 최대 1000ms까지 2배씩 증가

	// ─────────────────────────────────────────────
	// 스레드
	// ─────────────────────────────────────────────
	std::thread              mAcceptThread;  // 단일 accept 전담 스레드
	std::vector<std::thread> mWorkerThreads; // 완료 처리 워커 (hardware_concurrency개)
};

} // namespace Network::Platforms

#endif // __APPLE__
