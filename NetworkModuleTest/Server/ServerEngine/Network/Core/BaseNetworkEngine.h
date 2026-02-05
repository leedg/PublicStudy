#pragma once

// English: Base implementation of INetworkEngine with common logic
// 한글: 공통 로직을 포함한 INetworkEngine 기본 구현
//
// Design: Template Method Pattern
// - Common logic in base class (Session management, events, stats)
// - Platform-specific logic in derived classes (socket, I/O)
//
// 설계: 템플릿 메서드 패턴
// - 공통 로직은 기본 클래스 (Session 관리, 이벤트, 통계)
// - 플랫폼별 로직은 파생 클래스 (소켓, I/O)

#include "../../Utils/NetworkUtils.h"
#include "../../Utils/ThreadPool.h"
#include "AsyncIOProvider.h"
#include "NetworkEngine.h"
#include "Session.h"
#include "SessionManager.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Network::Core
{

// =============================================================================
// English: Base NetworkEngine - implements common functionality
// 한글: 기본 NetworkEngine - 공통 기능 구현
// =============================================================================

class BaseNetworkEngine : public INetworkEngine
{
  public:
	BaseNetworkEngine();
	virtual ~BaseNetworkEngine();

	// =====================================================================
	// English: INetworkEngine interface (final implementation)
	// 한글: INetworkEngine 인터페이스 (최종 구현)
	// =====================================================================

	bool Initialize(size_t maxConnections, uint16_t port) override final;
	bool Start() override final;
	void Stop() override final;
	bool IsRunning() const override final;

	bool RegisterEventCallback(NetworkEvent eventType,
								   NetworkEventCallback callback) override final;
	void UnregisterEventCallback(NetworkEvent eventType) override final;

	bool SendData(Utils::ConnectionId connectionId, const void *data,
				  size_t size) override final;
	void CloseConnection(Utils::ConnectionId connectionId) override final;
	std::string
	GetConnectionInfo(Utils::ConnectionId connectionId) const override final;

	Statistics GetStatistics() const override final;

  protected:
	// =====================================================================
	// English: Platform-specific hooks (must implement in derived classes)
	// 한글: 플랫폼별 훅 (파생 클래스에서 구현 필수)
	// =====================================================================

	/**
	 * English: Initialize platform-specific resources
	 * 한글: 플랫폼별 리소스 초기화
	 * @return True if initialization succeeded
	 */
	virtual bool InitializePlatform() = 0;

	/**
	 * English: Shutdown platform-specific resources
	 * 한글: 플랫폼별 리소스 종료
	 */
	virtual void ShutdownPlatform() = 0;

	/**
	 * English: Start platform-specific I/O threads
	 * 한글: 플랫폼별 I/O 스레드 시작
	 * @return True if start succeeded
	 */
	virtual bool StartPlatformIO() = 0;

	/**
	 * English: Stop platform-specific I/O threads
	 * 한글: 플랫폼별 I/O 스레드 중지
	 */
	virtual void StopPlatformIO() = 0;

	/**
	 * English: Accept loop (platform-specific)
	 * 한글: Accept 루프 (플랫폼별)
	 */
	virtual void AcceptLoop() = 0;

	/**
	 * English: Process I/O completions (platform-specific)
	 * 한글: I/O 완료 처리 (플랫폼별)
	 */
	virtual void ProcessCompletions() = 0;

	// =====================================================================
	// English: Helper methods for derived classes
	 // 한글: 파생 클래스용 헬퍼 메서드
	// =====================================================================

	/**
	 * English: Fire network event to registered callbacks
	 * 한글: 등록된 콜백에 네트워크 이벤트 발생
	 */
	void FireEvent(NetworkEvent eventType, Utils::ConnectionId connId,
				   const uint8_t *data = nullptr, size_t dataSize = 0,
				   OSError errorCode = 0);

	/**
	 * English: Process received data from completion
	 * 한글: 완료로부터 수신 데이터 처리
	 */
	void ProcessRecvCompletion(SessionRef session, int32_t bytesReceived,
								   const char *data);

	/**
	 * English: Process send completion
	 * 한글: 송신 완료 처리
	 */
	void ProcessSendCompletion(SessionRef session, int32_t bytesSent);

  protected:
	// =====================================================================
	// English: Common member variables
	// 한글: 공통 멤버 변수
	// =====================================================================

	// English: Async I/O provider (platform-specific backend)
	// 한글: 비동기 I/O 공급자 (플랫폼별 백엔드)
	std::unique_ptr<AsyncIO::AsyncIOProvider> mProvider;

	// English: Configuration
	// 한글: 설정
	uint16_t mPort;
	size_t mMaxConnections;

	// English: State
	// 한글: 상태
	std::atomic<bool> mRunning;
	std::atomic<bool> mInitialized;

	// English: Event callbacks
	// 한글: 이벤트 콜백
	std::unordered_map<NetworkEvent, NetworkEventCallback> mCallbacks;
	mutable std::mutex mCallbackMutex;

	// English: Logic thread pool (for async business logic)
	// 한글: 로직 스레드 풀 (비동기 비즈니스 로직용)
	Utils::ThreadPool mLogicThreadPool;

	// English: Statistics
	// 한글: 통계
	mutable std::mutex mStatsMutex;
	Statistics mStats;
};

} // namespace Network::Core
