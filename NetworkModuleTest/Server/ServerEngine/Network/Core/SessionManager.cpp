// English: SessionManager implementation
// 한글: SessionManager 구현

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

SessionRef SessionManager::CreateSession(SocketHandle socket)
{
	SessionRef session = SessionPool::Instance().Acquire();
	if (!session)
	{
		Utils::Logger::Error("SessionPool exhausted - no free session slots");
		return nullptr;
	}

	Utils::ConnectionId id = GenerateSessionId();
	session->Initialize(id, socket);

	{
		NET_LOCK_GUARD(mMutex);

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
	// English: Copy session list to avoid long lock duration
	// 한글: 긴 잠금 시간을 피하기 위해 세션 리스트 복사
	std::vector<SessionRef> sessionsCopy;
	{
		NET_LOCK_GUARD(mMutex);
		sessionsCopy.reserve(mSessions.size());
		for (auto &[id, session] : mSessions)
		{
			sessionsCopy.push_back(session);
		}
	}

	// English: Process sessions without holding lock
	// 한글: 잠금 없이 세션 처리
	for (auto &session : sessionsCopy)
	{
		func(session);
	}
}

std::vector<SessionRef> SessionManager::GetAllSessions()
{
	// English: Return snapshot of all sessions (caller owns the shared_ptr references)
	// 한글: 모든 세션의 스냅샷 반환 (호출자가 shared_ptr 참조 소유)
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
	// English: Copy session list to avoid deadlock (pattern same as ForEachSession)
	// 한글: 교착 상태 방지를 위해 세션 리스트 복사 (ForEachSession과 동일 패턴)
	// Deadlock scenario prevention:
	//   Thread A: CloseAllSessions() holds mMutex -> calls session->Close() -> waits for Session::mSendMutex
	//   Thread B: Session::Send() holds mSendMutex -> calls RemoveSession() -> waits for mMutex
	//   Result: DEADLOCK!
	// Solution: Release mMutex before calling session->Close()
	// 교착 상태 시나리오 방지:
	//   스레드 A: CloseAllSessions()가 mMutex 보유 -> session->Close() 호출 -> Session::mSendMutex 대기
	//   스레드 B: Session::Send()가 mSendMutex 보유 -> RemoveSession() 호출 -> mMutex 대기
	//   결과: 교착 상태!
	// 해결책: session->Close() 호출 전 mMutex 해제

	std::vector<SessionRef> sessionsCopy;
	{
		NET_LOCK_GUARD(mMutex);
		sessionsCopy.reserve(mSessions.size());
		for (auto &[id, session] : mSessions)
		{
			sessionsCopy.push_back(session);
		}
	}

	// English: Close sessions without holding mMutex
	// 한글: mMutex를 보유하지 않은 채로 세션 닫기
	for (auto &session : sessionsCopy)
	{
		if (session)
		{
			session->Close();
		}
	}

	// English: Clear session map after all sessions are closed
	// 한글: 모든 세션이 닫힌 후 세션 맵 정리
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

