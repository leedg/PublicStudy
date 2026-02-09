// English: Server packet handler implementation
// 한글: 서버 패킷 핸들러 구현

#include "../include/ServerPacketHandler.h"
#include "../include/OrderedTaskQueue.h"
#include "Utils/PingPongConfig.h"
#include <cstring>

namespace Network::DBServer
{
    using namespace Network::Core;
    using namespace Network::Utils;

    ServerPacketHandler::ServerPacketHandler()
        : mDBPingTimeManager(nullptr)
        , mLatencyManager(nullptr)
        , mOrderedTaskQueue(nullptr)
    {
        // English: Register all packet handlers
        // 한글: 모든 패킷 핸들러 등록
        RegisterHandlers();
    }

    ServerPacketHandler::~ServerPacketHandler()
    {
    }

    void ServerPacketHandler::Initialize(DBPingTimeManager* dbPingTimeManager,
                                          ServerLatencyManager* latencyManager,
                                          OrderedTaskQueue* orderedTaskQueue)
    {
        mDBPingTimeManager = dbPingTimeManager;
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

        const ServerPacketHeader* header = reinterpret_cast<const ServerPacketHeader*>(data);

        if (header->size > size)
        {
            Logger::Warn("Incomplete server packet - expected: " + std::to_string(header->size) +
                ", received: " + std::to_string(size));
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

        // English: Record latency via OrderedTaskQueue for ordering guarantee per serverId,
        //          or fall back to direct call if queue not available
        // 한글: serverId별 순서 보장을 위해 OrderedTaskQueue로 레이턴시 기록,
        //       큐를 사용할 수 없으면 직접 호출로 폴백
        if (mOrderedTaskQueue && mLatencyManager)
        {
            // English: Capture values for lambda (avoid dangling pointer)
            // 한글: 람다를 위한 값 캡처 (댕글링 포인터 방지)
            ServerLatencyManager* latencyMgr = mLatencyManager;
            DBPingTimeManager* pingTimeMgr = mDBPingTimeManager;
            uint64_t capturedRtt = rttMs;
            uint64_t capturedTime = receiveTime;
            uint32_t capturedServerId = serverId;

            mOrderedTaskQueue->EnqueueTask(serverId, [latencyMgr, pingTimeMgr,
                                                       capturedServerId, capturedRtt, capturedTime]()
            {
                // English: Record latency stats and persist to DB
                // 한글: 레이턴시 통계 기록 및 DB에 저장
                if (latencyMgr)
                {
                    latencyMgr->RecordLatency(capturedServerId,
                                               "Server_" + std::to_string(capturedServerId),
                                               capturedRtt, capturedTime);
                }

                // English: Also update ping time record
                // 한글: 핑 시간 기록도 업데이트
                if (pingTimeMgr && pingTimeMgr->IsInitialized())
                {
                    pingTimeMgr->SavePingTime(capturedServerId,
                                               "Server_" + std::to_string(capturedServerId),
                                               capturedTime);
                }
            });
        }
        else
        {
            // English: Fallback: direct latency recording (no ordering guarantee)
            // 한글: 폴백: 직접 레이턴시 기록 (순서 보장 없음)
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

        // English: Route through OrderedTaskQueue for per-server ordering guarantee
        // 한글: 서버별 순서 보장을 위해 OrderedTaskQueue를 통해 라우팅
        if (mOrderedTaskQueue && mDBPingTimeManager)
        {
            // English: Capture packet fields for async task
            // 한글: 비동기 작업을 위해 패킷 필드 캡처
            uint32_t capturedServerId = packet->serverId;
            std::string capturedServerName(packet->serverName);
            uint64_t capturedTimestamp = packet->timestamp;
            DBPingTimeManager* pingTimeMgr = mDBPingTimeManager;

            // English: Use weak_ptr to safely send response after async DB operation
            // 한글: 비동기 DB 작업 후 안전하게 응답을 보내기 위해 weak_ptr 사용
            auto sessionRef = session->shared_from_this();

            mOrderedTaskQueue->EnqueueTask(capturedServerId,
                [pingTimeMgr, sessionRef, capturedServerId, capturedServerName, capturedTimestamp]()
            {
                PKT_DBSavePingTimeRes asyncResponse;
                asyncResponse.serverId = capturedServerId;

                if (pingTimeMgr && pingTimeMgr->IsInitialized())
                {
                    bool success = pingTimeMgr->SavePingTime(
                        capturedServerId, capturedServerName, capturedTimestamp);

                    if (success)
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
                        Logger::Info("Ping time saved for ServerId: " + std::to_string(capturedServerId));
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
                        Logger::Error("Failed to save ping time for ServerId: " + std::to_string(capturedServerId));
                    }
                }
                else
                {
                    asyncResponse.result = 2;
#ifdef _WIN32
                    strncpy_s(asyncResponse.message, sizeof(asyncResponse.message),
                              "DB manager not initialized", _TRUNCATE);
#else
                    strncpy(asyncResponse.message, "DB manager not initialized",
                            sizeof(asyncResponse.message) - 1);
                    asyncResponse.message[sizeof(asyncResponse.message) - 1] = '\0';
#endif
                    Logger::Error("DB manager not initialized");
                }

                // English: Send response back on the captured session
                // 한글: 캡처된 세션으로 응답 전송
                if (sessionRef && sessionRef->IsConnected())
                {
                    sessionRef->Send(asyncResponse);
                }
            });
        }
        else
        {
            // English: Fallback: synchronous processing (original behavior)
            // 한글: 폴백: 동기 처리 (기존 동작)
            if (mDBPingTimeManager && mDBPingTimeManager->IsInitialized())
            {
                bool success = mDBPingTimeManager->SavePingTime(
                    packet->serverId, std::string(packet->serverName), packet->timestamp);

                if (success)
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
                          "DB manager not initialized", _TRUNCATE);
#else
                strncpy(response.message, "DB manager not initialized",
                        sizeof(response.message) - 1);
                response.message[sizeof(response.message) - 1] = '\0';
#endif
            }

            session->Send(response);
        }
    }

} // namespace Network::DBServer
