// SessionManager 구현

#include "SessionManager.h"
#include "SessionPool.h"
#include "Utils/KeyGenerator.h"
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
	// 한글: 풀 슬롯 획득 전에 세션 한도를 확인.
	//       여기서 nullptr을 반환하면 소켓이 세션 객체에 아직 전달되지 않은 상태이므로
	//       호출자가 소켓 닫기 책임을 진다.
	//       Initialize() 이후에 확인하면 풀 deleter가 Close()로 소켓을 닫은 뒤
	//       호출자도 close(fd)를 호출하여 이중 닫기가 발생한다.
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
	// 한글: PostRecv() 이전에 애플리케이션 수준 설정 적용.
	//       mSessions에 등록되기 전 호출하므로 recv 완료보다 먼저 실행됨을 보장.
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

	// race condition 방지를 위해 ID를 먼저 저장
	// (Close()가 mId를 0으로 바꾸기 전에 캡처)
	Utils::ConnectionId id = session->GetId();

	// 리소스 정리를 위해 제거 전 세션 닫기
	if (session->IsConnected())
	{
		session->Close();
	}

	// 미리 저장한 ID로 매니저에서 제거
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
	// 긴 잠금 시간을 피하기 위해 세션 리스트를 복사한 후 락 해제
	std::vector<SessionRef> sessionsCopy;
	{
		NET_LOCK_GUARD(mMutex);
		sessionsCopy.reserve(mSessions.size());
		for (auto &[id, session] : mSessions)
		{
			sessionsCopy.push_back(session);
		}
	}

	// 잠금 없이 세션 처리
	for (auto &session : sessionsCopy)
	{
		func(session);
	}
}

std::vector<SessionRef> SessionManager::GetAllSessions()
{
	// 모든 세션의 스냅샷 반환 (호출자가 shared_ptr 참조 소유)
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
	// 교착 상태 방지를 위해 세션 리스트를 복사한 후 mMutex 해제.
	// 교착 상태 시나리오:
	//   스레드 A: CloseAllSessions()가 mMutex 보유 → session->Close() 호출 → Session::mSendMutex 대기
	//   스레드 B: Session::Send()가 mSendMutex 보유 → RemoveSession() 호출 → mMutex 대기
	// 해결: session->Close() 호출 전 mMutex를 해제하여 락 역전을 방지.

	std::vector<SessionRef> sessionsCopy;
	{
		NET_LOCK_GUARD(mMutex);
		sessionsCopy.reserve(mSessions.size());
		for (auto &[id, session] : mSessions)
		{
			sessionsCopy.push_back(session);
		}
	}

	// mMutex를 보유하지 않은 채로 세션 닫기
	for (auto &session : sessionsCopy)
	{
		if (session)
		{
			session->Close();
		}
	}

	// 모든 세션이 닫힌 후 세션 맵 정리
	{
		NET_LOCK_GUARD(mMutex);
		mSessions.clear();
		Utils::Logger::Info("All sessions closed - Count: " + std::to_string(sessionsCopy.size()));
	}
}

Utils::ConnectionId SessionManager::GenerateSessionId()
{
	// KeyGenerator::NextGlobalId() — monotonic uint64_t, lock-free, starts at 1.
	// 한글: KeyGenerator 전역 ID — 단조 증가 uint64_t, 락-프리, 1부터 시작.
	return Utils::KeyGenerator::NextGlobalId();
}

} // namespace Network::Core

