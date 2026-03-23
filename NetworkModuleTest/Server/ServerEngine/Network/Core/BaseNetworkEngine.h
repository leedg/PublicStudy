#pragma once

// 공통 로직을 포함한 INetworkEngine 기본 구현
//
// 설계: 템플릿 메서드 패턴
// - 공통 로직(Session 관리, 이벤트, 통계)은 기본 클래스가 담당
// - 플랫폼별 로직(소켓, I/O 완료 처리)은 파생 클래스가 구현

#include "../../Concurrency/KeyedDispatcher.h"
#include "../../Concurrency/TimerQueue.h"
#include "../../Utils/NetworkUtils.h"
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
// 기본 NetworkEngine — 공통 기능 구현
// =============================================================================

class BaseNetworkEngine : public INetworkEngine
{
  public:
	BaseNetworkEngine();
	virtual ~BaseNetworkEngine();

	// =====================================================================
	// INetworkEngine 인터페이스 (최종 구현)
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
	// 플랫폼별 훅 (파생 클래스에서 구현 필수)
	// =====================================================================

	/** 플랫폼별 리소스 초기화 */
	virtual bool InitializePlatform() = 0;

	/** 플랫폼별 리소스 종료 */
	virtual void ShutdownPlatform() = 0;

	/** 플랫폼별 I/O 스레드 시작 */
	virtual bool StartPlatformIO() = 0;

	/** 플랫폼별 I/O 스레드 중지 */
	virtual void StopPlatformIO() = 0;

	/** Accept 루프 (플랫폼별) */
	virtual void AcceptLoop() = 0;

	/** I/O 완료 처리 (플랫폼별) */
	virtual void ProcessCompletions() = 0;

	// =====================================================================
	// 파생 클래스용 헬퍼 메서드
	// =====================================================================

	/** 등록된 콜백과 NetworkEventBus에 네트워크 이벤트 발행 */
	void FireEvent(NetworkEvent eventType, Utils::ConnectionId connId,
				   const uint8_t *data = nullptr, size_t dataSize = 0,
				   OSError errorCode = 0);

	/** 수신 완료(IOCP/epoll) 처리. bytesReceived <= 0이면 연결 종료로 간주. */
	void ProcessRecvCompletion(SessionRef session, int32_t bytesReceived,
								   const char *data);

	/** 송신 완료 처리. bytesSent <= 0이면 ProcessErrorCompletion으로 라우팅. */
	void ProcessSendCompletion(SessionRef session, int32_t bytesSent);

	/**
	 * I/O 에러 완료 처리.
	 * 방향별 에러 카운터(mTotalSendErrors 또는 mTotalRecvErrors)를 증가시키고,
	 * ProcessRecvCompletion(0)을 통해 disconnect를 라우팅하여
	 * session->mAsyncScope를 항상 준수한다.
	 * OS 에러나 non-positive result를 가진 completion entry에서는
	 * ProcessRecvCompletion(session, 0, nullptr)을 직접 호출하는 대신 이 함수를 사용해
	 * Send/Recv 에러를 분리 집계한다.
	 * @param session  에러가 발생한 세션
	 * @param ioType   AsyncIOType::Send or AsyncIOType::Recv
	 * @param osError  OS 수준 에러 코드 (없으면 0)
	 */
	void ProcessErrorCompletion(SessionRef session,
	                            AsyncIO::AsyncIOType ioType,
	                            OSError osError);

  protected:
	// =====================================================================
	// 공통 멤버 변수
	// =====================================================================

	// 비동기 I/O 공급자 (플랫폼별 백엔드).
	// Session이 SetAsyncProvider()를 통해 복사본을 보유하므로 shared_ptr을 사용한다.
	// 엔진이 Shutdown()되더라도 세션이 참조를 놓을 때까지 공급자 객체가 살아있는다.
	std::shared_ptr<AsyncIO::AsyncIOProvider> mProvider;

	// 설정
	uint16_t mPort;
	size_t mMaxConnections;

	// 상태
	std::atomic<bool> mRunning;
	std::atomic<bool> mInitialized;

	// 이벤트 콜백
	std::unordered_map<NetworkEvent, NetworkEventCallback> mCallbacks;
	mutable std::mutex mCallbackMutex;

	// 순서 보장 비동기 로직 실행을 위한 키 친화도 디스패처.
	// 동일 sessionId는 항상 같은 워커로 라우팅되어 세션 단위 FIFO 순서가 보장된다.
	// 이로 인해 ProcessRawRecv와 Close()가 동일 세션에 대해 직렬화된다.
	Network::Concurrency::KeyedDispatcher mLogicDispatcher;

	// 엔진 수준 타이머 큐.
	// 세션 타임아웃 점검(PING_TIMEOUT_MS/2 주기) 등 주기 작업에 사용된다.
	Network::Concurrency::TimerQueue mTimerQueue;

	// 통계 — 핫 패스 카운터는 atomic, 콜드 패스 데이터는 mutex 사용.
	// Send/Recv 에러 카운터를 분리 집계하여 GetStatistics()에서 방향별 분류를 제공한다.
	// totalErrors = sendErrors + recvErrors.
	mutable std::mutex mStatsMutex;
	Statistics mStats;
	std::atomic<uint64_t> mTotalBytesSent{0};
	std::atomic<uint64_t> mTotalBytesReceived{0};
	std::atomic<uint64_t> mTotalConnections{0};
	std::atomic<uint64_t> mTotalSendErrors{0};
	std::atomic<uint64_t> mTotalRecvErrors{0};
};

} // namespace Network::Core
