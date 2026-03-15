#pragma once

// Session manager for creating/tracking/removing sessions

#include "Session.h"
#include <functional>
#include <unordered_map>

namespace Network::Core
{
// =============================================================================
// SessionManager class
// =============================================================================

class SessionManager
{
  public:
	static SessionManager &Instance();

	// Session lifecycle
	SessionRef CreateSession(SocketHandle socket);
	void RemoveSession(Utils::ConnectionId id);
	void RemoveSession(SessionRef session);

	// Session lookup
	SessionRef GetSession(Utils::ConnectionId id);

	// Iterate all sessions (const reference to avoid function object copy)
	void ForEachSession(const std::function<void(SessionRef)>& func);

	// Get snapshot of all sessions (for race-free iteration)
	std::vector<SessionRef> GetAllSessions();

	// Session count
	size_t GetSessionCount() const;

	// Close all sessions
	void CloseAllSessions();

	// Register a one-time configurator invoked inside CreateSession, after
	//          Initialize() and before PostRecv().  Use this to attach per-session
	//          callbacks (e.g. SetOnRecv) so that no recv completion can fire before
	//          the callback is set.  Replaces the removed SessionFactory pattern.
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
