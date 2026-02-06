// English: SessionManager implementation
// ?��?: SessionManager 구현

#include "SessionManager.h"
#include <sstream>

namespace Network::Core
{

SessionManager &SessionManager::Instance()
{
	static SessionManager instance;
	return instance;
}

void SessionManager::Initialize(SessionFactory factory)
{
	mSessionFactory = std::move(factory);
	Utils::Logger::Info("SessionManager initialized");
}

SessionRef SessionManager::CreateSession(SocketHandle socket)
{
	if (!mSessionFactory)
	{
		Utils::Logger::Error("Session factory not set");
		return nullptr;
	}

	SessionRef session = mSessionFactory();
	if (!session)
	{
		Utils::Logger::Error("Failed to create session from factory");
		return nullptr;
	}

	Utils::ConnectionId id = GenerateSessionId();
	session->Initialize(id, socket);

	{
		NET_LOCK_GUARD();

		if (mSessions.size() >= Utils::MAX_CONNECTIONS)
		{
			Utils::Logger::Warn("Max session count reached");
			return nullptr;
		}

		mSessions[id] = session;
	}

	Utils::Logger::Info("Session created - ID: " + std::to_string(id) +
						", Total: " + std::to_string(GetSessionCount()));

	return session;
}

void SessionManager::RemoveSession(Utils::ConnectionId id)
{
	NET_LOCK_GUARD();

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

	// English: Store ID first to avoid race condition
	// 한글: Race condition 방지를 위해 ID를 먼저 저장
	Utils::ConnectionId id = session->GetId();

	// English: Close session before removing to cleanup resources
	// 한글: 리소스 정리를 위해 제거 전 세션 닫기
	if (session->IsConnected())
	{
		session->Close();
	}

	// English: Remove from manager using pre-stored ID
	// 한글: 미리 저장한 ID를 사용하여 매니저에서 제거
	RemoveSession(id);
}

SessionRef SessionManager::GetSession(Utils::ConnectionId id)
{
	NET_LOCK_GUARD();

	auto it = mSessions.find(id);
	if (it != mSessions.end())
	{
		return it->second;
	}

	return nullptr;
}

void SessionManager::ForEachSession(std::function<void(SessionRef)> func)
{
	// English: Copy session list to avoid long lock duration
	// ?��?: �??�금 ?�간???�하�??�해 ?�션 리스??복사
	std::vector<SessionRef> sessionsCopy;
	{
		NET_LOCK_GUARD();
		sessionsCopy.reserve(mSessions.size());
		for (auto &[id, session] : mSessions)
		{
			sessionsCopy.push_back(session);
		}
	}

	// English: Process sessions without holding lock
	// ?��?: ?�금 ?�이 ?�션 처리
	for (auto &session : sessionsCopy)
	{
		func(session);
	}
}

size_t SessionManager::GetSessionCount() const
{
	NET_LOCK_GUARD();
	return mSessions.size();
}

void SessionManager::CloseAllSessions()
{
	// English: Copy session list to avoid deadlock (pattern same as ForEachSession)
	// ?��?: 교착 ?�태 방�?�??�해 ?�션 리스??복사 (ForEachSession�??�일 ?�턴)
	// Deadlock scenario prevention:
	//   Thread A: CloseAllSessions() holds mMutex -> calls session->Close() -> waits for Session::mSendMutex
	//   Thread B: Session::Send() holds mSendMutex -> calls RemoveSession() -> waits for mMutex
	//   Result: DEADLOCK!
	// Solution: Release mMutex before calling session->Close()
	// 교착 ?�태 ?�나리오 방�?:
	//   ?�레??A: CloseAllSessions()가 mMutex 보유 -> session->Close() ?�출 -> Session::mSendMutex ?��?
	//   ?�레??B: Session::Send()가 mSendMutex 보유 -> RemoveSession() ?�출 -> mMutex ?��?
	//   결과: 교착 ?�태!
	// ?�결�? session->Close() ?�출 ??mMutex ?�제

	std::vector<SessionRef> sessionsCopy;
	{
		NET_LOCK_GUARD();
		sessionsCopy.reserve(mSessions.size());
		for (auto &[id, session] : mSessions)
		{
			sessionsCopy.push_back(session);
		}
	}

	// English: Close sessions without holding mMutex
	// ?��?: mMutex�?보유?��? ?��? 채로 ?�션 ?�기
	for (auto &session : sessionsCopy)
	{
		if (session)
		{
			session->Close();
		}
	}

	// English: Clear session map after all sessions are closed
	// ?��?: 모든 ?�션???�힌 ???�션 �??�리
	{
		NET_LOCK_GUARD();
		mSessions.clear();
		Utils::Logger::Info("All sessions closed - Count: " + std::to_string(sessionsCopy.size()));
	}
}

Utils::ConnectionId SessionManager::GenerateSessionId()
{
	return mNextSessionId.fetch_add(1);
}

} // namespace Network::Core

