// Server packet handler implementation

#include "../include/ServerPacketHandler.h"
#include "../include/OrderedTaskQueue.h"
#include "Utils/PingPongConfig.h"
// DBPingTimeManager include removed — functionality merged into ServerLatencyManager
#include <cstring>

namespace Network::DBServer
{
    using namespace Network::Core;
    using namespace Network::Utils;

    ServerPacketHandler::ServerPacketHandler()
        : mLatencyManager(nullptr)
        , mOrderedTaskQueue(nullptr)
    {
        // Register all packet handlers at construction time
        RegisterHandlers();
    }

    ServerPacketHandler::~ServerPacketHandler()
    {
    }

    void ServerPacketHandler::Initialize(ServerLatencyManager* latencyManager,
                                          OrderedTaskQueue* orderedTaskQueue)
    {
        // DBPingTimeManager is no longer a separate dependency —
        //          its functionality is now in ServerLatencyManager.
        mLatencyManager = latencyManager;
        mOrderedTaskQueue = orderedTaskQueue;
    }

    void ServerPacketHandler::RegisterHandlers()
    {
        // Register handler functors for each packet type
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

        // Ensure that mOrderedTaskQueue and mLatencyManager are initialized
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

        // Validate minimal packet size per server packet id.
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

        // Use functor map to dispatch packet handler
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
        // Validate pointers
        if (!session || !packet)
        {
            Logger::Error("HandleServerPingRequest: null pointer");
            return;
        }

        // Calculate RTT - use request timestamp vs current time for latency
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

        // Send pong response immediately (low latency path)
        PKT_ServerPongRes response;
        response.sequence = packet->sequence;
        response.requestTimestamp = packet->timestamp;
        response.responseTimestamp = receiveTime;

        session->Send(response);

#ifdef ENABLE_PINGPONG_VERBOSE_LOG
        Logger::Debug("Server pong sent - Seq: " + std::to_string(packet->sequence));
#endif

        // Derive serverId from session's connection ID for per-server tracking
        uint32_t serverId = static_cast<uint32_t>(session->GetId());

        // Route through OrderedTaskQueue for per-serverId ordering guarantee.
        //          RecordLatency covers both RTT stats AND ping-time persistence now
        //          (DBPingTimeManager merged into ServerLatencyManager).
        //          Fall back to a direct call when the queue is not available.
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
            // Fallback: direct call (no per-serverId ordering guarantee)
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
        // Validate pointers
        if (!session || !packet)
        {
            Logger::Error("HandleDBSavePingTimeRequest: null pointer");
            return;
        }

        Logger::Info("DB save ping time request - ServerId: " + std::to_string(packet->serverId) +
            ", ServerName: " + std::string(packet->serverName));

        PKT_DBSavePingTimeRes response;
        response.serverId = packet->serverId;

        // Route through OrderedTaskQueue for per-serverId ordering guarantee.
        //          SavePingTime now lives in ServerLatencyManager (DBPingTimeManager merged).
        //          Fall back to synchronous processing when the queue is not available.
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
                            std::snprintf(asyncResponse.message, sizeof(asyncResponse.message),
                                          "Ping time saved successfully");
                            Logger::Info("Ping time saved for ServerId: " +
                                         std::to_string(capturedServerId));
                        }
                        else
                        {
                            asyncResponse.result = 1;
                            std::snprintf(asyncResponse.message, sizeof(asyncResponse.message),
                                          "Failed to save ping time");
                            Logger::Error("Failed to save ping time for ServerId: " +
                                          std::to_string(capturedServerId));
                        }
                    }
                    else
                    {
                        asyncResponse.result = 2;
                        std::snprintf(asyncResponse.message, sizeof(asyncResponse.message),
                                      "Latency manager not initialized");
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
            // Fallback: synchronous processing (no queue available)
            if (mLatencyManager && mLatencyManager->IsInitialized())
            {
                const bool ok = mLatencyManager->SavePingTime(
                    packet->serverId, std::string(packet->serverName), packet->timestamp);

                if (ok)
                {
                    response.result = 0;
                    std::snprintf(response.message, sizeof(response.message),
                                  "Ping time saved successfully");
                }
                else
                {
                    response.result = 1;
                    std::snprintf(response.message, sizeof(response.message),
                                  "Failed to save ping time");
                }
            }
            else
            {
                response.result = 2;
                std::snprintf(response.message, sizeof(response.message),
                              "Latency manager not initialized");
            }

            session->Send(response);
        }
    }

} // namespace Network::DBServer
