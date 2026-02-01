#pragma once

// English: Session manager for creating/tracking/removing sessions
// ?쒓?: ?몄뀡 ?앹꽦/異붿쟻/?쒓굅瑜??꾪븳 ?몄뀡 愿由ъ옄

#include "Session.h"
#include <unordered_map>
#include <functional>

namespace Network::Core
{
    // =============================================================================
    // English: Session factory function type
    // ?쒓?: ?몄뀡 ?⑺넗由??⑥닔 ???
    // =============================================================================

    using SessionFactory = std::function<SessionRef()>;

    // =============================================================================
    // English: SessionManager class
    // ?쒓?: SessionManager ?대옒??
    // =============================================================================

    class SessionManager
    {
    public:
        static SessionManager& Instance();

        // English: Set session factory (must be called before CreateSession)
        // ?쒓?: ?몄뀡 ?⑺넗由??ㅼ젙 (CreateSession ?몄텧 ?꾩뿉 ?ㅼ젙 ?꾩슂)
        void Initialize(SessionFactory factory);

        // English: Session lifecycle
        // ?쒓?: ?몄뀡 ?앸챸二쇨린
        SessionRef CreateSession(SocketHandle socket);
        void RemoveSession(Utils::ConnectionId id);
        void RemoveSession(SessionRef session);

        // English: Session lookup
        // ?쒓?: ?몄뀡 議고쉶
        SessionRef GetSession(Utils::ConnectionId id);

        // English: Iterate all sessions
        // ?쒓?: 紐⑤뱺 ?몄뀡 ?쒗쉶
        void ForEachSession(std::function<void(SessionRef)> func);

        // English: Session count
        // ?쒓?: ?몄뀡 ??
        size_t GetSessionCount() const;

        // English: Close all sessions
        // ?쒓?: 紐⑤뱺 ?몄뀡 醫낅즺
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

