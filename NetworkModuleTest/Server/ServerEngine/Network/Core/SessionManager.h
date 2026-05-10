#pragma once

// 세션 생성/추적/제거를 위한 세션 관리자

#include "Session.h"
#include <functional>
#include <unordered_map>

namespace Network::Core
{
// =============================================================================
// SessionManager 클래스
// =============================================================================

class SessionManager
{
  public:
	static SessionManager &Instance();

	// 세션 생명주기
	SessionRef CreateSession(SocketHandle socket);
	void RemoveSession(Utils::ConnectionId id);
	void RemoveSession(SessionRef session);

	// 세션 조회
	SessionRef GetSession(Utils::ConnectionId id);

	// 모든 세션 순회 (함수 객체 복사 방지를 위한 const reference)
	void ForEachSession(const std::function<void(SessionRef)>& func);

	// 모든 세션의 스냅샷 취득 (경합 조건 없는 순회용)
	std::vector<SessionRef> GetAllSessions();

	// 세션 수
	size_t GetSessionCount() const;

	// 모든 세션 종료
	void CloseAllSessions();

	// Register a one-time configurator invoked inside CreateSession, after
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
	std::unordered_map<Utils::ConnectionId, SessionRef> mSessions;          // 활성 세션 맵 (mMutex 보호)
	mutable std::mutex                                  mMutex;             // mSessions 읽기·쓰기 보호
	std::function<void(Session *)>                      mSessionConfigurator;  // CreateSession 시 1회 호출 설정 콜백
};

} // namespace Network::Core
