#pragma once

// English: Session manager for creating/tracking/removing sessions
// 한글: 세션 생성/추적/제거를 위한 세션 관리자

#include "Session.h"
#include <unordered_map>
#include <functional>

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
        static SessionManager& Instance();

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

        // English: Iterate all sessions
        // 한글: 모든 세션 순회
        void ForEachSession(std::function<void(SessionRef)> func);

        // English: Session count
        // 한글: 세션 수
        size_t GetSessionCount() const;

        // English: Close all sessions
        // 한글: 모든 세션 종료
        void CloseAllSessions();

    private:
        SessionManager() = default;
        ~SessionManager() = default;

        SessionManager(const SessionManager&) = delete;
        SessionManager& operator=(const SessionManager&) = delete;

        Utils::ConnectionId GenerateSessionId();

    private:
        std::unordered_map<Utils::ConnectionId, SessionRef> mSessions;
        mutable std::mutex                                   mMutex;
        std::atomic<Utils::ConnectionId>                     mNextSessionId{1};
        SessionFactory                                       mSessionFactory;
    };

} // namespace Network::Core
