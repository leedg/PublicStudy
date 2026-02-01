// English: Session class implementation
// ?쒓?: Session ?대옒??援ы쁽

#include "Session.h"
#include <sstream>
#include <iostream>
#include <cstring>

namespace Network::Core
{

Session::Session()
    : mId(0)
    , mSocket(
#ifdef _WIN32
        INVALID_SOCKET
#else
        -1
#endif
    )
    , mState(SessionState::None)
    , mConnectTime(0)
    , mLastPingTime(0)
    , mPingSequence(0)
    , mIsSending(false)
#ifdef _WIN32
    , mRecvContext(IOType::Recv)
    , mSendContext(IOType::Send)
#endif
{
}

Session::~Session()
{
    Close();
}

void Session::Initialize(Utils::ConnectionId id, SocketHandle socket)
{
    mId = id;
    mSocket = socket;
    mState = SessionState::Connected;
    mConnectTime = Utils::Timer::GetCurrentTimestamp();
    mLastPingTime = mConnectTime;
    mPingSequence = 0;
    mIsSending = false;

    Utils::Logger::Info("Session initialized - ID: " + std::to_string(mId));
}

void Session::Close()
{
    if (mState == SessionState::Disconnected)
    {
        return;
    }

    mState = SessionState::Disconnected;

    if (mSocket !=
#ifdef _WIN32
        INVALID_SOCKET
#else
        -1
#endif
    )
    {
#ifdef _WIN32
        closesocket(mSocket);
        mSocket = INVALID_SOCKET;
#else
        close(mSocket);
        mSocket = -1;
#endif
    }

    // English: Clear send queue
    // ?쒓?: ?꾩넚 ??鍮꾩슦湲?
    {
        std::lock_guard<std::mutex> lock(mSendMutex);
        while (!mSendQueue.empty())
        {
            mSendQueue.pop();
        }
    }

    Utils::Logger::Info("Session closed - ID: " + std::to_string(mId));
}

void Session::Send(const void* data, uint32_t size)
{
    if (!IsConnected() || data == nullptr || size == 0)
    {
        return;
    }

    // English: Enqueue send data
    // ?쒓?: ?꾩넚 ?곗씠???먯엵
    {
        std::lock_guard<std::mutex> lock(mSendMutex);
        std::vector<char> buffer(size);
        std::memcpy(buffer.data(), data, size);
        mSendQueue.push(std::move(buffer));
    }

    FlushSendQueue();
}

void Session::FlushSendQueue()
{
    // English: CAS to prevent concurrent sends
    // ?쒓?: CAS濡??숈떆 ?꾩넚 諛⑹?
    bool expected = false;
    if (!mIsSending.compare_exchange_strong(expected, true))
    {
        return;
    }

    PostSend();
}

bool Session::PostSend()
{
#ifdef _WIN32
    std::vector<char> dataToSend;

    {
        std::lock_guard<std::mutex> lock(mSendMutex);

        if (mSendQueue.empty())
        {
            mIsSending = false;
            return true;
        }

        dataToSend = std::move(mSendQueue.front());
        mSendQueue.pop();
    }

    mSendContext.Reset();
    std::memcpy(mSendContext.buffer, dataToSend.data(), dataToSend.size());
    mSendContext.wsaBuf.buf = mSendContext.buffer;
    mSendContext.wsaBuf.len = static_cast<ULONG>(dataToSend.size());

    DWORD bytesSent = 0;
    int result = WSASend(
        mSocket,
        &mSendContext.wsaBuf,
        1,
        &bytesSent,
        0,
        &mSendContext,
        nullptr
    );

    if (result == SOCKET_ERROR)
    {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING)
        {
            Utils::Logger::Error("WSASend failed - Error: " + std::to_string(error));
            mIsSending = false;
            return false;
        }
    }

    return true;
#else
    // English: Linux/macOS implementation (placeholder)
    // ?쒓?: Linux/macOS 援ы쁽 (?뚮젅?댁뒪???
    mIsSending = false;
    return false;
#endif
}

bool Session::PostRecv()
{
#ifdef _WIN32
    if (!IsConnected())
    {
        return false;
    }

    mRecvContext.Reset();
    mRecvContext.wsaBuf.buf = mRecvContext.buffer;
    mRecvContext.wsaBuf.len = sizeof(mRecvContext.buffer);

    DWORD bytesReceived = 0;
    DWORD flags = 0;

    int result = WSARecv(
        mSocket,
        &mRecvContext.wsaBuf,
        1,
        &bytesReceived,
        &flags,
        &mRecvContext,
        nullptr
    );

    if (result == SOCKET_ERROR)
    {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING)
        {
            Utils::Logger::Error("WSARecv failed - Error: " + std::to_string(error));
            return false;
        }
    }

    return true;
#else
    return false;
#endif
}

} // namespace Network::Core

