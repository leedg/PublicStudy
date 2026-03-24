#pragma once

// Linux 전용 NetworkEngine 구현.
//
// 두 가지 I/O 백엔드를 지원한다:
// - Epoll  : 모든 Linux 버전에서 동작하는 표준 이벤트 알림.
//            커널이 fd 상태 변화를 통지하면 AcceptLoop / WorkerThread가 처리한다.
// - IOUring: Linux 5.1+ 의 고성능 비동기 I/O.
//            빌드 시스템(CMake find_library)이 HAVE_IO_URING 또는 HAVE_LIBURING를
//            정의한 경우에만 활성화되며, 정의되지 않았거나 런타임 초기화에 실패하면
//            자동으로 epoll로 폴백한다.
//
// 선택 기준:
//   - 커널 5.1 미만이거나 liburing 미설치 환경  → Epoll
//   - 커널 5.1 이상 + liburing 설치 + 고처리량 요구 → IOUring

#ifdef __linux__

#include "../Core/BaseNetworkEngine.h"
#include <thread>
#include <vector>

namespace Network::Platforms
{

class LinuxNetworkEngine : public Core::BaseNetworkEngine
{
  public:
	// I/O 백엔드 모드
	enum class Mode
	{
		Epoll,   // 표준 epoll (커널 2.6+)
		IOUring  // io_uring (커널 5.1+, liburing 필요)
	};

	// @param mode  사용할 I/O 백엔드. IOUring 선택 시 런타임에 지원 여부를 확인하고
	//              실패하면 Epoll로 자동 폴백한다.
	explicit LinuxNetworkEngine(Mode mode = Mode::Epoll);
	virtual ~LinuxNetworkEngine();

  protected:
	// 플랫폼별 구현 (BaseNetworkEngine 순수 가상 재정의)
	bool InitializePlatform() override;
	void ShutdownPlatform() override;
	bool StartPlatformIO() override;
	void StopPlatformIO() override;
	void AcceptLoop() override;
	void ProcessCompletions() override;

  private:
	// listen 소켓 생성 및 바인드
	bool CreateListenSocket();

	// 세션에 recv 작업 등록 (epoll EPOLLIN 또는 io_uring RecvAsync)
	bool QueueRecv(const Core::SessionRef &session);

	// 완료 처리 루프 (WorkerThread 내부에서 반복 호출)
	void WorkerThread();

  private:
	// ─────────────────────────────────────────────
	// 백엔드 및 소켓
	// ─────────────────────────────────────────────
	Mode mMode;         // 선택된 I/O 백엔드 — IOUring 런타임 실패 시 Epoll로 변경될 수 있음
	int  mListenSocket; // TCP listen fd — POSIX fd; -1 = 미초기화

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

#endif // __linux__
