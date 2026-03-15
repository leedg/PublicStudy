#pragma once

// English: Session manager for creating/tracking/removing sessions
// 한글: 세션 생성/추적/제거를 위한 세션 관리자

#include "Session.h"
#include <functional>
#include <unordered_map>

namespace Network::Core
{
// =============================================================================
// English: SessionManager class
// 한글: SessionManager 클래스
// =============================================================================

class SessionManager
{
  public:
	static SessionManager &Instance();

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

	// English: Register a one-time configurator invoked inside CreateSession, after
	//          Initialize() and before PostRecv().  Use this to attach per-session
	//          callbacks (e.g. SetOnRecv) so that no recv completion can fire before
	//          the callback is set.  Replaces the removed SessionFactory pattern.
	// 한글: CreateSession 내에서 Initialize() 이후, PostRecv() 이전에 한 번 호출되는
	//       설정 콜백 등록. 세션별 콜백(예: SetOnRecv)을 첫 recv 완료 이전에 안전하게
	//       등록하기 위해 사용. 제거된 SessionFactory 패턴을 대체.
	void SetSessionConfigurator(std::function<void(Session *)> configurator);

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
	std::function<void(Session *)> mSessionConfigurator;
};

} // namespace Network::Core
