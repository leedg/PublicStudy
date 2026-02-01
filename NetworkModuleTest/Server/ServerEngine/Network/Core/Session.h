#pragma once

// English: Client session class for connection management
// ?쒓?: ?곌껐 愿由щ? ?꾪븳 ?대씪?댁뼵???몄뀡 ?대옒??

#include "AsyncIOProvider.h"
#include "PacketDefine.h"
#include "../../Utils/NetworkUtils.h"
#include <memory>
#include <mutex>
#include <queue>
#include <vector>
#include <atomic>

namespace Network::Core
{
    // =============================================================================
    // English: Session state
    // ?쒓?: ?몄뀡 ?곹깭
    // =============================================================================

    enum class SessionState : uint8_t
    {
        None,
        Connecting,
        Connected,
        Disconnecting,
        Disconnected,
    };

    // =============================================================================
    // English: IO operation type
    // ?쒓?: IO ?묒뾽 ???
    // =============================================================================

    enum class IOType : uint8_t
    {
        Accept,
        Recv,
        Send,
        Disconnect,
    };

    // =============================================================================
    // English: IOCP overlapped context (Windows only)
    // ?쒓?: IOCP ?ㅻ쾭??而⑦뀓?ㅽ듃 (Windows ?꾩슜)
    // =============================================================================

#ifdef _WIN32

    struct IOContext : public OVERLAPPED
    {
        IOType type;
        WSABUF wsaBuf;
        char   buffer[RECV_BUFFER_SIZE];

        IOContext(IOType ioType)
            : type(ioType)
        {
            memset(static_cast<OVERLAPPED*>(this), 0, sizeof(OVERLAPPED));
            memset(buffer, 0, sizeof(buffer));
            wsaBuf.buf = buffer;
            wsaBuf.len = sizeof(buffer);
        }

        void Reset()
        {
            memset(static_cast<OVERLAPPED*>(this), 0, sizeof(OVERLAPPED));
        }
    };

#endif // _WIN32

    // =============================================================================
    // English: Session class
    // ?쒓?: ?몄뀡 ?대옒??
    // =============================================================================

    class Session : public std::enable_shared_from_this<Session>
    {
    public:
        Session();
        virtual ~Session();

        // English: Lifecycle
        // ?쒓?: ?앸챸二쇨린
        void Initialize(Utils::ConnectionId id, SocketHandle socket);
        void Close();

        // English: Send packet
        // ?쒓?: ?⑦궥 ?꾩넚
        void Send(const void* data, uint32_t size);

        template<typename T>
        void Send(const T& packet)
        {
            Send(&packet, sizeof(T));
        }

        // English: Post receive request to IOCP
        // ?쒓?: IOCP???섏떊 ?붿껌 ?깅줉
        bool PostRecv();

        // English: Accessors
        // ?쒓?: ?묎렐??
        Utils::ConnectionId GetId() const { return mId; }
        SocketHandle GetSocket() const { return mSocket; }
        SessionState GetState() const { return mState; }
        bool IsConnected() const { return mState == SessionState::Connected; }

        Utils::Timestamp GetConnectTime() const { return mConnectTime; }
        Utils::Timestamp GetLastPingTime() const { return mLastPingTime; }
        void SetLastPingTime(Utils::Timestamp time) { mLastPingTime = time; }
        uint32_t GetPingSequence() const { return mPingSequence; }
        void IncrementPingSequence() { ++mPingSequence; }

        // English: Access recv buffer (for IOCP completion)
        // ?쒓?: ?섏떊 踰꾪띁 ?묎렐 (IOCP ?꾨즺 泥섎━??
#ifdef _WIN32
        IOContext& GetRecvContext() { return mRecvContext; }
        IOContext& GetSendContext() { return mSendContext; }
#endif

        // English: Virtual event handlers (override in derived classes)
        // ?쒓?: 媛???대깽???몃뱾??(?뚯깮 ?대옒?ㅼ뿉???ㅻ쾭?쇱씠??
        virtual void OnConnected() {}
        virtual void OnDisconnected() {}
        virtual void OnRecv(const char* data, uint32_t size) {}

    private:
        // English: Internal send processing
        // ?쒓?: ?대? ?꾩넚 泥섎━
        void FlushSendQueue();
        bool PostSend();

    private:
        Utils::ConnectionId     mId;
        SocketHandle            mSocket;
        SessionState            mState;

        // English: Time tracking
        // ?쒓?: ?쒓컙 異붿쟻
        Utils::Timestamp        mConnectTime;
        Utils::Timestamp        mLastPingTime;
        uint32_t                mPingSequence;

        // English: IO contexts (Windows IOCP)
        // ?쒓?: IO 而⑦뀓?ㅽ듃 (Windows IOCP)
#ifdef _WIN32
        IOContext                mRecvContext;
        IOContext                mSendContext;
#endif

        // English: Send queue
        // ?쒓?: ?꾩넚 ??
        std::queue<std::vector<char>>   mSendQueue;
        std::mutex                      mSendMutex;
        std::atomic<bool>               mIsSending;
    };

    using SessionRef = std::shared_ptr<Session>;
    using SessionWeakRef = std::weak_ptr<Session>;

} // namespace Network::Core

