#pragma once

// English: Session manager for creating/tracking/removing sessions
// 한글: 세션 생성/추적/제거를 위한 세션 관리자

#include "Session.h"
#include <functional>
#include <unordered_map>

namespace Network::Core
{
// =============================================================================
// English: Session factory function type
// 한글: 세션 팩토리 함수 타입
// =============================================================================

using SessionFactory = std::function<SessionRef()>;

// =============================================================================
// English: SessionManager class
// 한글: SessionManager 클래스
// =============================================================================

class SessionManager
{
  public:
	static SessionManager &Instance();

	// English: Set session factory (must be called before CreateSession)
	// 한글: 세션 팩토리 설정 (CreateSession 호출 전에 설정 필요)
	void Initialize(SessionFactory factory);

	// English: Session lifecycle
	// 한글: 세션 생명주기
	SessionRef CreateSession(SocketHandle socket);
	void RemoveSession(Utils::ConnectionId id);
	void RemoveSession(SessionRef session);

	// English: Session lookup
	// 한글: 세션 조회
	SessionRef GetSession(Utils::ConnectionId id);

	// English: Iterate all sessions (const reference to avoid function object copy)
	// 한글: 모든 세션 순회 (함수 객체 복사 방지를 위한 const reference)
	void ForEachSession(const std::function<void(SessionRef)>& func);

	// English: Get snapshot of all sessions (for race-free iteration)
	// 한글: 모든 세션의 스냅샷 취득 (경합 조건 없는 순회용)
	std::vector<SessionRef> GetAllSessions();

	// English: Session count
	// 한글: 세션 수
	size_t GetSessionCount() const;

	// English: Close all sessions
	// 한글: 모든 세션 종료
	void CloseAllSessions();

  private:
	SessionManager() = default;
	~SessionManager() = default;

	SessionManager(const SessionManager &) = delete;
	SessionManager &operator=(const SessionManager &) = delete;

	Utils::ConnectionId GenerateSessionId();

  private:
	std::unordered_map<Utils::ConnectionId, SessionRef> mSessions;
	mutable std::mutex mMutex;
	std::atomic<Utils::ConnectionId> mNextSessionId{1};
	SessionFactory mSessionFactory;
};

} // namespace Network::Core
