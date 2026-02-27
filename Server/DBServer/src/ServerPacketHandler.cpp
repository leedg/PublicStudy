// English: Server packet handler implementation
// 한글: 서버 패킷 핸들러 구현

#include "../include/ServerPacketHandler.h"
#include "../include/OrderedTaskQueue.h"
#include "Utils/PingPongConfig.h"
// English: DBPingTimeManager include removed — functionality merged into ServerLatencyManager
// 한글: DBPingTimeManager include 제거 — 기능이 ServerLatencyManager에 통합됨
#include <cstring>

namespace Network::DBServer
{
    using namespace Network::Core;
    using namespace Network::Utils;

    ServerPacketHandler::ServerPacketHandler()
        : mLatencyManager(nullptr)
        , mOrderedTaskQueue(nullptr)
    {
        // English: Register all packet handlers at construction time
        // 한글: 생성 시 모든 패킷 핸들러 등록
        RegisterHandlers();
    }

    ServerPacketHandler::~ServerPacketHandler()
    {
    }

    void ServerPacketHandler::Initialize(ServerLatencyManager* latencyManager,
                                          OrderedTaskQueue* orderedTaskQueue)
    {
        // English: DBPingTimeManager is no longer a separate dependency —
        //          its functionality is now in ServerLatencyManager.
        // 한글: DBPingTimeManager는 더 이상 별도 의존성이 아님 —
        //       기능이 ServerLatencyManager에 통합됨.
        mLatencyManager = latencyManager;
        mOrderedTaskQueue = orderedTaskQueue;
    }

    void ServerPacketHandler::RegisterHandlers()
    {
        // English: Register handler functors for each packet type
        // 한글: 각 패킷 타입에 대한 핸들러 펑터 등록
        mHandlers[static_cast<uint16_t>(ServerPacketType::ServerPingReq)] =
            [this](Core::Session* session, const char* data, uint32_t size)
            {
                HandleServerPingRequest(session, reinterpret_cast<const PKT_ServerPingReq*>(data));
            };

        mHandlers[static_cast<uint16_t>(ServerPacketType::DBSavePingTimeReq)] =
            [this](Core::Session* session, const char* data, uint32_t size)
            {
                HandleDBSavePingTimeRequest(session, reinterpret_cast<const PKT_DBSavePingTimeReq*>(data));
            };
    }

    void ServerPacketHandler::ProcessPacket(Core::Session* session, const char* data, uint32_t size)
    {
        if (!session || !data || size < sizeof(ServerPacketHeader))
        {
            Logger::Warn("Invalid server packet data");
            return;
        }

        // English: Ensure that mOrderedTaskQueue and mLatencyManager are initialized
        // 한글: mOrderedTaskQueue와 mLatencyManager가 초기화되었는지 확인
        if (!mOrderedTaskQueue || !mLatencyManager)
        {
            Logger::Error("ServerPacketHandler not properly initialized - missing OrderedTaskQueue or LatencyManager");
            return;
        }

        const ServerPacketHeader* header = reinterpret_cast<const ServerPacketHeader*>(data);

        if (header->size < sizeof(ServerPacketHeader) || header->size > 4096)
        {
            Logger::Warn("Server packet size out of range: " + std::to_string(header->size));
            return;
        }

        if (header->size > size)
        {
            Logger::Warn("Incomplete server packet - expected: " + std::to_string(header->size) +
                ", received: " + std::to_string(size));
            return;
        }

        // English: Validate minimal packet size per server packet id.
        // 한글: 서버 패킷 ID별 최소 길이 검증.
        uint32_t requiredSize = sizeof(ServerPacketHeader);
        switch (static_cast<ServerPacketType>(header->id))
        {
        case ServerPacketType::ServerPingReq:
            requiredSize = sizeof(PKT_ServerPingReq);
            break;
        case ServerPacketType::DBSavePingTimeReq:
            requiredSize = sizeof(PKT_DBSavePingTimeReq);
            break;
        default:
            break;
        }

        if (header->size < requiredSize)
        {
            Logger::Warn("Server packet too small for id " + std::to_string(header->id) +
                " - expected at least: " + std::to_string(requiredSize) +
                ", actual: " + std::to_string(header->size));
            return;
        }

        // English: Use functor map to dispatch packet handler
        // 한글: 펑터 맵을 사용하여 패킷 핸들러 디스패치
        auto it = mHandlers.find(header->id);
        if (it != mHandlers.end())
        {
            it->second(session, data, size);
        }
        else
        {
            Logger::Warn("Unknown packet type from game server: " + std::to_string(header->id));
        }
    }

    void ServerPacketHandler::HandleServerPingRequest(Core::Session* session, const PKT_ServerPingReq* packet)
    {
        // English: Validate pointers
        // 한글: 포인터 유효성 검사
        if (!session || !packet)
        {
            Logger::Error("HandleServerPingRequest: null pointer");
            return;
        }

        // English: Calculate RTT - use request timestamp vs current time for latency
        // 한글: RTT 계산 - 요청 타임스탬프와 현재 시간으로 레이턴시 측정
        uint64_t receiveTime = Timer::GetCurrentTimestamp();
        uint64_t rttMs = receiveTime - packet->timestamp;

#ifdef ENABLE_PINGPONG_VERBOSE_LOG
        Logger::Debug("Server ping received - Seq: " + std::to_string(packet->sequence) +
                     ", Latency: " + std::to_string(rttMs) + "ms");
#else
        if (packet->sequence % PINGPONG_LOG_INTERVAL == 0)
        {
            Logger::Info("[DBServer] Ping received (every " + std::to_string(PINGPONG_LOG_INTERVAL) +
                "th) - Seq: " + std::to_string(packet->sequence) +
                ", Latency: " + std::to_string(rttMs) + "ms");
        }
#endif

        // English: Send pong response immediately (low latency path)
        // 한글: 퐁 응답 즉시 전송 (저지연 경로)
        PKT_ServerPongRes response;
        response.sequence = packet->sequence;
        response.requestTimestamp = packet->timestamp;
        response.responseTimestamp = receiveTime;

        session->Send(response);

#ifdef ENABLE_PINGPONG_VERBOSE_LOG
        Logger::Debug("Server pong sent - Seq: " + std::to_string(packet->sequence));
#endif

        // English: Derive serverId from session's connection ID for per-server tracking
        // 한글: 서버별 추적을 위해 세션의 연결 ID에서 serverId 유도
        uint32_t serverId = static_cast<uint32_t>(session->GetId());

        // English: Route through OrderedTaskQueue for per-serverId ordering guarantee.
        //          RecordLatency covers both RTT stats AND ping-time persistence now
        //          (DBPingTimeManager merged into ServerLatencyManager).
        //          Fall back to a direct call when the queue is not available.
        // 한글: serverId별 순서 보장을 위해 OrderedTaskQueue를 통해 라우팅.
        //       RecordLatency가 RTT 통계와 핑 시간 저장을 모두 처리
        //       (DBPingTimeManager가 ServerLatencyManager에 통합됨).
        //       큐를 사용할 수 없으면 직접 호출로 폴백.
        if (mOrderedTaskQueue && mLatencyManager)
        {
            ServerLatencyManager* latencyMgr = mLatencyManager;
            const uint64_t capturedRtt     = rttMs;
            const uint64_t capturedTime    = receiveTime;
            const uint32_t capturedServerId = serverId;

            mOrderedTaskQueue->EnqueueTask(serverId,
                [latencyMgr, capturedServerId, capturedRtt, capturedTime]()
                {
                    if (!latencyMgr || !latencyMgr->IsInitialized()) return;
                    latencyMgr->RecordLatency(capturedServerId,
                                              "Server_" + std::to_string(capturedServerId),
                                              capturedRtt, capturedTime);
                });
        }
        else
        {
            // English: Fallback: direct call (no per-serverId ordering guarantee)
            // 한글: 폴백: 직접 호출 (serverId별 순서 보장 없음)
            if (mLatencyManager)
            {
                mLatencyManager->RecordLatency(serverId,
                                               "Server_" + std::to_string(serverId),
                                               rttMs, receiveTime);
            }
        }
    }

    void ServerPacketHandler::HandleDBSavePingTimeRequest(Core::Session* session, const PKT_DBSavePingTimeReq* packet)
    {
        // English: Validate pointers
        // 한글: 포인터 유효성 검사
        if (!session || !packet)
        {
            Logger::Error("HandleDBSavePingTimeRequest: null pointer");
            return;
        }

        Logger::Info("DB save ping time request - ServerId: " + std::to_string(packet->serverId) +
            ", ServerName: " + std::string(packet->serverName));

        PKT_DBSavePingTimeRes response;
        response.serverId = packet->serverId;

        // English: Route through OrderedTaskQueue for per-serverId ordering guarantee.
        //          SavePingTime now lives in ServerLatencyManager (DBPingTimeManager merged).
        //          Fall back to synchronous processing when the queue is not available.
        // 한글: serverId별 순서 보장을 위해 OrderedTaskQueue를 통해 라우팅.
        //       SavePingTime은 이제 ServerLatencyManager에 있음 (DBPingTimeManager 통합).
        //       큐를 사용할 수 없으면 동기 처리로 폴백.
        if (mOrderedTaskQueue && mLatencyManager)
        {
            const uint32_t    capturedServerId   = packet->serverId;
            const std::string capturedServerName(packet->serverName);
            const uint64_t    capturedTimestamp  = packet->timestamp;
            ServerLatencyManager* latencyMgr     = mLatencyManager;
            auto sessionRef = session->shared_from_this();

            mOrderedTaskQueue->EnqueueTask(capturedServerId,
                [latencyMgr, sessionRef, capturedServerId, capturedServerName, capturedTimestamp]()
                {
                    if (!sessionRef || !sessionRef->IsConnected()) return;
                    PKT_DBSavePingTimeRes asyncResponse;
                    asyncResponse.serverId = capturedServerId;

                    if (latencyMgr && latencyMgr->IsInitialized())
                    {
                        const bool ok = latencyMgr->SavePingTime(
                            capturedServerId, capturedServerName, capturedTimestamp);

                        if (ok)
                        {
                            asyncResponse.result = 0;
#ifdef _WIN32
                            strncpy_s(asyncResponse.message, sizeof(asyncResponse.message),
                                      "Ping time saved successfully", _TRUNCATE);
#else
                            strncpy(asyncResponse.message, "Ping time saved successfully",
                                    sizeof(asyncResponse.message) - 1);
                            asyncResponse.message[sizeof(asyncResponse.message) - 1] = '\0';
#endif
                            Logger::Info("Ping time saved for ServerId: " +
                                         std::to_string(capturedServerId));
                        }
                        else
                        {
                            asyncResponse.result = 1;
#ifdef _WIN32
                            strncpy_s(asyncResponse.message, sizeof(asyncResponse.message),
                                      "Failed to save ping time", _TRUNCATE);
#else
                            strncpy(asyncResponse.message, "Failed to save ping time",
                                    sizeof(asyncResponse.message) - 1);
                            asyncResponse.message[sizeof(asyncResponse.message) - 1] = '\0';
#endif
                            Logger::Error("Failed to save ping time for ServerId: " +
                                          std::to_string(capturedServerId));
                        }
                    }
                    else
                    {
                        asyncResponse.result = 2;
#ifdef _WIN32
                        strncpy_s(asyncResponse.message, sizeof(asyncResponse.message),
                                  "Latency manager not initialized", _TRUNCATE);
#else
                        strncpy(asyncResponse.message, "Latency manager not initialized",
                                sizeof(asyncResponse.message) - 1);
                        asyncResponse.message[sizeof(asyncResponse.message) - 1] = '\0';
#endif
                        Logger::Error("Latency manager not initialized");
                    }

                    if (sessionRef && sessionRef->IsConnected())
                    {
                        sessionRef->Send(asyncResponse);
                    }
                });
        }
        else
        {
            // English: Fallback: synchronous processing (no queue available)
            // 한글: 폴백: 동기 처리 (큐 없음)
            if (mLatencyManager && mLatencyManager->IsInitialized())
            {
                const bool ok = mLatencyManager->SavePingTime(
                    packet->serverId, std::string(packet->serverName), packet->timestamp);

                if (ok)
                {
                    response.result = 0;
#ifdef _WIN32
                    strncpy_s(response.message, sizeof(response.message),
                              "Ping time saved successfully", _TRUNCATE);
#else
                    strncpy(response.message, "Ping time saved successfully",
                            sizeof(response.message) - 1);
                    response.message[sizeof(response.message) - 1] = '\0';
#endif
                }
                else
                {
                    response.result = 1;
#ifdef _WIN32
                    strncpy_s(response.message, sizeof(response.message),
                              "Failed to save ping time", _TRUNCATE);
#else
                    strncpy(response.message, "Failed to save ping time",
                            sizeof(response.message) - 1);
                    response.message[sizeof(response.message) - 1] = '\0';
#endif
                }
            }
            else
            {
                response.result = 2;
#ifdef _WIN32
                strncpy_s(response.message, sizeof(response.message),
                          "Latency manager not initialized", _TRUNCATE);
#else
                strncpy(response.message, "Latency manager not initialized",
                        sizeof(response.message) - 1);
                response.message[sizeof(response.message) - 1] = '\0';
#endif
            }

            session->Send(response);
        }
    }

} // namespace Network::DBServer
