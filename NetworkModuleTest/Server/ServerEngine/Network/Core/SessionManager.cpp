// English: SessionManager implementation
// ?쒓?: SessionManager 援ы쁽

#include "SessionManager.h"
#include <sstream>

namespace Network::Core
{

SessionManager& SessionManager::Instance()
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
        std::lock_guard<std::mutex> lock(mMutex);

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
    std::lock_guard<std::mutex> lock(mMutex);

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
    if (session)
    {
        RemoveSession(session->GetId());
    }
}

SessionRef SessionManager::GetSession(Utils::ConnectionId id)
{
    std::lock_guard<std::mutex> lock(mMutex);

    auto it = mSessions.find(id);
    if (it != mSessions.end())
    {
        return it->second;
    }

    return nullptr;
}

void SessionManager::ForEachSession(std::function<void(SessionRef)> func)
{
    std::lock_guard<std::mutex> lock(mMutex);

    for (auto& [id, session] : mSessions)
    {
        func(session);
    }
}

size_t SessionManager::GetSessionCount() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mSessions.size();
}

void SessionManager::CloseAllSessions()
{
    std::lock_guard<std::mutex> lock(mMutex);

    for (auto& [id, session] : mSessions)
    {
        session->Close();
    }

    mSessions.clear();
    Utils::Logger::Info("All sessions closed");
}

Utils::ConnectionId SessionManager::GenerateSessionId()
{
    return mNextSessionId.fetch_add(1);
}

} // namespace Network::Core

