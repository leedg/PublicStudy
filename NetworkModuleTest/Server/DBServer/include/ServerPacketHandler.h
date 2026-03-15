#pragma once

// Server packet handler for TestDBServer

#include "Network/Core/Session.h"
#include "Network/Core/ServerPacketDefine.h"
#include "ServerLatencyManager.h"   // unified latency + ping-time manager (DBPingTimeManager merged in)
#include "Utils/NetworkUtils.h"
#include <functional>
#include <unordered_map>
#include <memory>

// Forward declaration
namespace Network::DBServer { class OrderedTaskQueue; }

namespace Network::DBServer
{
    using Utils::ConnectionId;

    // =============================================================================
    // ServerPacketHandler - handles packets from game servers using functor map.
    //
    //   Dependencies (all injected, not owned):
    //     - ServerLatencyManager  : unified RTT stats + ping time persistence
    //                               (previously required both ServerLatencyManager AND
    //                                DBPingTimeManager — the latter is now merged in)
    //     - OrderedTaskQueue      : hash-based thread affinity for per-serverId ordering
    //
    //
    // =============================================================================

    class ServerPacketHandler
    {
    public:
        // Packet handler functor type
        using PacketHandlerFunc = std::function<void(Core::Session*, const char*, uint32_t)>;

        ServerPacketHandler();
        virtual ~ServerPacketHandler();

        // Initialize with unified latency manager and ordered task queue.
        //          DBPingTimeManager is no longer a separate parameter — it has been
        //          merged into ServerLatencyManager.
        void Initialize(ServerLatencyManager* latencyManager,
                        OrderedTaskQueue* orderedTaskQueue);

        // Process incoming packet from game server (uses functor dispatch)
        void ProcessPacket(Core::Session* session, const char* data, uint32_t size);

    private:
        // Packet handler functor map (ServerPacketType -> Handler)
        std::unordered_map<uint16_t, PacketHandlerFunc> mHandlers;

        // Register all packet handlers
        void RegisterHandlers();

        // Individual packet handlers
        void HandleServerPingRequest(Core::Session* session, const Core::PKT_ServerPingReq* packet);
        void HandleDBSavePingTimeRequest(Core::Session* session, const Core::PKT_DBSavePingTimeReq* packet);

    private:
        ServerLatencyManager* mLatencyManager;        // Not owned — unified RTT + ping time
        OrderedTaskQueue*     mOrderedTaskQueue;      // Not owned — per-serverId ordering
    };

} // namespace Network::DBServer
