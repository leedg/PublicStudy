// SessionManager implementation

#include "SessionManager.h"
#include "SessionPool.h"
#include "Utils/LockProfiling.h"
#include <sstream>

namespace Network::Core
{

SessionManager &SessionManager::Instance()
{
	static SessionManager instance;
	return instance;
}

void SessionManager::SetSessionConfigurator(std::function<void(Session *)> configurator)
{
	mSessionConfigurator = std::move(configurator);
}

SessionRef SessionManager::CreateSession(SocketHandle socket)
{
	// Check session limit BEFORE acquiring a pool slot.
	//          If we return nullptr here, the socket has NOT been given to any session
	//          object — the caller is responsible for closing it.
	//          Checking after Initialize() would cause the pool deleter to call Close()
	//          (closing the socket) while the caller also calls close(fd) — double-close.
	{
		NET_LOCK_GUARD(mMutex);
		if (mSessions.size() >= Utils::MAX_CONNECTIONS)
		{
			Utils::Logger::Warn("Max session count reached");
			return nullptr;
		}
	}

	SessionRef session = SessionPool::Instance().Acquire();
	if (!session)
	{
		Utils::Logger::Error("SessionPool exhausted - no free session slots");
		return nullptr;
	}

	Utils::ConnectionId id = GenerateSessionId();
	session->Initialize(id, socket);

	// Apply application-level configuration before PostRecv() is issued.
	//          Called here (before the session enters mSessions) so recv completions
	//          cannot fire before the callback is set.
	if (mSessionConfigurator)
		mSessionConfigurator(session.get());

	{
		NET_LOCK_GUARD(mMutex);
		mSessions[id] = session;
	}

	Utils::Logger::Info("Session created - ID: " + std::to_string(id) +
						", Total: " + std::to_string(GetSessionCount()));

	return session;
}

void SessionManager::RemoveSession(Utils::ConnectionId id)
{
	NET_LOCK_GUARD(mMutex);

	auto it = mSessions.find(id);
	if (it != mSessions.end())
	{
		mSessions.erase(it);
		Utils::Logger::Info("Session removed - ID: " + std::to_string(id) +
							", Remaining: " + std::to_string(mSessions.size()));
	}
}

void SessionManager::RemoveSession(SessionRef session)
{
	if (!session)
	{
		return;
	}

	// Store ID first to avoid race condition
	Utils::ConnectionId id = session->GetId();

	// Close session before removing to cleanup resources
	if (session->IsConnected())
	{
		session->Close();
	}

	// Remove from manager using pre-stored ID
	RemoveSession(id);
}

SessionRef SessionManager::GetSession(Utils::ConnectionId id)
{
	NET_LOCK_GUARD(mMutex);

	auto it = mSessions.find(id);
	if (it != mSessions.end())
	{
		return it->second;
	}

	return nullptr;
}

void SessionManager::ForEachSession(const std::function<void(SessionRef)>& func)
{
	// Copy session list to avoid long lock duration
	std::vector<SessionRef> sessionsCopy;
	{
		NET_LOCK_GUARD(mMutex);
		sessionsCopy.reserve(mSessions.size());
		for (auto &[id, session] : mSessions)
		{
			sessionsCopy.push_back(session);
		}
	}

	// Process sessions without holding lock
	for (auto &session : sessionsCopy)
	{
		func(session);
	}
}

std::vector<SessionRef> SessionManager::GetAllSessions()
{
	// Return snapshot of all sessions (caller owns the shared_ptr references)
	std::vector<SessionRef> sessionsCopy;
	NET_LOCK_GUARD(mMutex);
	sessionsCopy.reserve(mSessions.size());
	for (auto &[id, session] : mSessions)
	{
		sessionsCopy.push_back(session);
	}
	return sessionsCopy;
}

size_t SessionManager::GetSessionCount() const
{
	NET_LOCK_GUARD(mMutex);
	return mSessions.size();
}

void SessionManager::CloseAllSessions()
{
	// Copy session list to avoid deadlock (pattern same as ForEachSession)
	// Deadlock scenario prevention:
	//   Thread A: CloseAllSessions() holds mMutex -> calls session->Close() -> waits for Session::mSendMutex
	//   Thread B: Session::Send() holds mSendMutex -> calls RemoveSession() -> waits for mMutex
	//   Result: DEADLOCK!
	// Solution: Release mMutex before calling session->Close()

	std::vector<SessionRef> sessionsCopy;
	{
		NET_LOCK_GUARD(mMutex);
		sessionsCopy.reserve(mSessions.size());
		for (auto &[id, session] : mSessions)
		{
			sessionsCopy.push_back(session);
		}
	}

	// Close sessions without holding mMutex
	for (auto &session : sessionsCopy)
	{
		if (session)
		{
			session->Close();
		}
	}

	// Clear session map after all sessions are closed
	{
		NET_LOCK_GUARD(mMutex);
		mSessions.clear();
		Utils::Logger::Info("All sessions closed - Count: " + std::to_string(sessionsCopy.size()));
	}
}

Utils::ConnectionId SessionManager::GenerateSessionId()
{
	return mNextSessionId.fetch_add(1);
}

} // namespace Network::Core

