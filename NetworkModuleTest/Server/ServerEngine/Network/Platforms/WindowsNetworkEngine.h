#pragma once

// Windows 전용 NetworkEngine 구현.
//
// 두 가지 I/O 백엔드를 지원한다:
// - IOCP: 모든 Windows 버전에서 동작하는 표준 I/O 완료 포트.
//         WSASend/WSARecv + OVERLAPPED 구조를 사용하며,
//         IocpAsyncIOProvider가 내부적으로 사전 할당 없이 동적 버퍼를 관리한다.
// - RIO : Windows 8+ 전용 고성능 Registered I/O.
//         VirtualAlloc으로 할당한 슬랩 메모리를 RIORegisterBuffer로 한 번만 등록하고
//         이후 I/O마다 재등록 없이 재사용한다.
//         WSA 10055 (WSAENOBUFS) — 커널 non-paged pool 소진 — 오류는 대부분 IOCP에서
//         per-op 버퍼 고정(pin)이 중첩될 때 발생한다. RIO의 사전 등록 슬랩 풀은
//         이 오류를 원천적으로 방지한다.
//
// 선택 기준:
//   - Windows 7 이하 또는 호환성 우선   → IOCP
//   - Windows 8+ + 고처리량 / 저지연 요구 → RIO

#ifdef _WIN32

#include "../Core/BaseNetworkEngine.h"
#include <thread>
#include <vector>

namespace Network::Platforms
{

class WindowsNetworkEngine : public Core::BaseNetworkEngine
{
  public:
	// I/O 백엔드 모드
	enum class Mode
	{
		IOCP, // 표준 I/O 완료 포트 (모든 Windows)
		RIO   // Registered I/O (Windows 8+, 고성능)
	};

	// @param mode  사용할 I/O 백엔드. 기본값 IOCP.
	explicit WindowsNetworkEngine(Mode mode = Mode::IOCP);
	virtual ~WindowsNetworkEngine();

  protected:
	// 플랫폼별 구현 (BaseNetworkEngine 순수 가상 재정의)
	bool InitializePlatform() override;
	void ShutdownPlatform() override;
	bool StartPlatformIO() override;
	void StopPlatformIO() override;
	void AcceptLoop() override;
	void ProcessCompletions() override;

  private:
	// WSAStartup(2.2) 호출 및 Winsock 초기화
	bool InitializeWinsock();

	// listen 소켓 생성 및 바인드.
	// RIO 모드에서는 WSA_FLAG_REGISTERED_IO 플래그 추가.
	bool CreateListenSocket();

	// 완료 처리 루프 (WorkerThread 내부에서 반복 호출)
	void WorkerThread();

  private:
	Mode   mMode;        // IOCP 또는 RIO
	SOCKET mListenSocket; // INVALID_SOCKET = 미초기화

	// Accept 루프에서 연속 오류 발생 시 지수 백오프용 대기 시간(ms).
	// static 변수 대신 멤버 변수로 유지하여 재진입/복수 인스턴스 버그를 방지한다.
	int mAcceptBackoffMs;

	std::thread              mAcceptThread;  // 단일 accept 전담 스레드
	std::vector<std::thread> mWorkerThreads; // 완료 처리 워커 (hardware_concurrency개)
};

} // namespace Network::Platforms

#endif // _WIN32
