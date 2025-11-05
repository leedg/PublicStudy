#pragma once
#include <cstdint>

// === Tuning Parameters ===
namespace Config {
    // Networking
    inline uint16_t  ListenPortDefault = 5050;
    inline int       Backlog           = SOMAXCONN;
    inline int       MaxPendingAccept  = 512;
    inline bool      TcpNoDelay        = true;

    // RIO & Workers
    inline int       WorkerCount       = -1;
    inline bool      UsePolling        = true;
    inline uint32_t  CQSizePerWorker   = 65536;

    // Outstanding per session
    inline int       RecvOutstandingPerSession = 128;
    inline int       SendOutstandingPerSession = 128;

    // Buffer pool
    inline uint32_t  SliceSize         = 1024;
    inline uint64_t  PoolBytes         = 64ull * 1024ull * 1024ull;

    // Polling wait
    inline int       PollBusySpinIters = 200;
    inline int       PollSleepMicros   = 0;
}
