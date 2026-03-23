#pragma once

// 데이터베이스 서버 메인 헤더

// DBPingTimeManager는 ServerLatencyManager로 통합됨 — 마이그레이션 노트는 DBPingTimeManager.h 참조
#include "ServerLatencyManager.h"

// IDatabase 전방 선언; DBServer.cpp에서 전체 정의 포함
namespace Network { namespace Database { class IDatabase; } }

// 로컬 config용 DatabaseType
#include "../ServerEngine/Interfaces/DatabaseConfig.h"
#include "../ServerEngine/Utils/NetworkTypes.h"
#include "../ServerEngine/Network/Core/AsyncIOProvider.h"
#include "../ServerEngine/Tests/Protocols/MessageHandler.h"
#include "../ServerEngine/Tests/Protocols/PingPong.h"
#include <mutex>
#include <string>
#include <unordered_map>
#include <atomic>
#include <memory>
#include <thread>

namespace Network::DBServer
{
// Protocols의 ConnectionId를 DBServer 범위에서 재사용한다.
using Protocols::ConnectionId;
// =============================================================================
// 데이터베이스 서버 클래스
// =============================================================================

class DBServer
{
  private:
	struct DatabaseConfig;

  public:
	DBServer();
	~DBServer();

	// =====================================================================
	// 생명주기 관리
	// =====================================================================

	/**
	 * 데이터베이스 서버 초기화.
	 * @param port 리슨할 포트 번호
	 * @param maxConnections 최대 허용 연결 수
	 * @return 초기화 성공 시 true
	 */
	bool Initialize(uint16_t port = Network::Utils::DEFAULT_TEST_DB_PORT, size_t maxConnections = 1000);

	/**
	 * 데이터베이스 서버 시작.
	 * @return 서버 시작 성공 시 true
	 */
	bool Start();

	/**
	 * 데이터베이스 서버 정지.
	 */
	void Stop();

	/**
	 * 서버 실행 중 여부 확인.
	 * @return 서버 실행 중이면 true
	 */
	bool IsRunning() const;

	// =====================================================================
	// 설정
	// =====================================================================

	/**
	 * 데이터베이스 연결 파라미터 설정.
	 * @param host 데이터베이스 호스트
	 * @param port 데이터베이스 포트
	 * @param database 데이터베이스 이름
	 * @param username 사용자 이름
	 * @param password 비밀번호
	 */
	void SetDatabaseConfig(const std::string &host, uint16_t port,
							   const std::string &database,
							   const std::string &username,
							   const std::string &password);

	/**
	 * 다음 DB 연결 시도에서 사용할 백엔드 타입 설정.
	 * @param type 데이터베이스 백엔드 타입
	 */
	void SetDatabaseType(Network::Database::DatabaseType type);

	void SetDatabaseSqlDialectHint(
		Network::Database::SqlDialect sqlDialectHint);

	/**
	 * DSN / Driver 기반 완전한 연결 문자열 설정.
	 * @param type 데이터베이스 백엔드 타입
	 * @param connectionString 완전한 연결 문자열
	 */
	void SetDatabaseConnectionString(Network::Database::DatabaseType type,
										 const std::string &connectionString,
										 Network::Database::SqlDialect sqlDialectHint =
											 Network::Database::SqlDialect::Auto);

  private:
	// =====================================================================
	// 네트워크 이벤트 핸들러
	// =====================================================================

	/**
	 * 신규 연결 처리.
	 * @param connectionId 연결 ID
	 */
	void OnConnectionEstablished(ConnectionId connectionId);

	/**
	 * 연결 종료 처리.
	 * @param connectionId 연결 ID
	 */
	void OnConnectionClosed(ConnectionId connectionId);

	/**
	 * 수신 데이터 처리.
	 * @param connectionId 연결 ID
	 * @param data 수신 데이터
	 * @param size 데이터 크기
	 */
	void OnDataReceived(ConnectionId connectionId, const uint8_t *data,
						size_t size);

	/**
	 * Ping 메시지 처리.
	 * @param message Ping 메시지
	 */
	void OnPingMessage(const Protocols::Message &message);

	/**
	 * Pong 메시지 처리.
	 * @param message Pong 메시지
	 */
	void OnPongMessage(const Protocols::Message &message);

	// =====================================================================
	// 데이터베이스 작업
	// =====================================================================

	/**
	 * 데이터베이스 연결.
	 * @return 연결 성공 시 true
	 */
	bool ConnectToDatabase();

	/**
	 * 현재 DB 설정으로 런타임 연결 문자열 구성.
	 */
	static std::string BuildConnectionString(const DatabaseConfig &config);

	/**
	 * 데이터베이스 연결 해제.
	 */
	void DisconnectFromDatabase();

	/**
	 * 쿼리 실행.
	 * @param query SQL 쿼리
	 * @return 쿼리 결과
	 */
	std::string ExecuteQuery(const std::string &query);

	// =====================================================================
	// 내부 멤버
	// =====================================================================

	// Network components
	std::unique_ptr<AsyncIO::AsyncIOProvider> mAsyncIOProvider;
	std::unique_ptr<Protocols::MessageHandler> mMessageHandler;
	std::unique_ptr<Protocols::PingPongHandler> mPingPongHandler;
	// Ping/Pong 레이턴시 + 시간 저장 — ServerLatencyManager로 통합 (이전: DBPingTimeManager)
	std::unique_ptr<ServerLatencyManager> mLatencyManager;

	// Server state
	std::atomic<bool> mIsRunning;
	std::atomic<bool> mIsInitialized;
	uint16_t mPort;
	size_t mMaxConnections;

	// Database configuration
	struct DatabaseConfig
	{
		std::string host = "localhost";
		uint16_t port = 5432;
		std::string database = "networkdb";
		std::string username = "postgres";
		std::string password = "password";
		std::string connectionString;
		// 외부 DB 없이 바로 동작하도록 기본값을 Mock으로 설정
		Network::Database::DatabaseType type = Network::Database::DatabaseType::Mock;
		Network::Database::SqlDialect sqlDialectHint =
			Network::Database::SqlDialect::Auto;
	} mDbConfig;

	// ConnectToDatabase가 생성하는 소유 데이터베이스 인스턴스
	std::unique_ptr<Network::Database::IDatabase> mDatabase;

	// Worker thread
	std::thread mWorkerThread;

	// Connection management
	std::unordered_map<ConnectionId, std::string> mConnections;
	std::mutex mConnectionsMutex;

	// =====================================================================
	// 내부 메서드
	// =====================================================================

	/**
	 * 워커 스레드 함수.
	 */
	void WorkerThread();

	/**
	 * 연결에 메시지 전송.
	 * @param connectionId 연결 ID
	 * @param type 메시지 타입
	 * @param data 메시지 데이터
	 * @param size 데이터 크기
	 */
	void SendMessage(ConnectionId connectionId, Protocols::MessageType type,
					 const void *data, size_t size);

	/**
	 * 현재 타임스탬프 반환 (밀리초).
	 * @return 밀리초 단위 타임스탬프
	 */
	uint64_t GetCurrentTimestamp() const;
};

} // namespace Network::DBServer
