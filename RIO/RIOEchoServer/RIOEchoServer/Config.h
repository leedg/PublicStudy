#pragma once
#include <cstdint>

namespace Config {
    inline uint16_t  ListenPortDefault = 5050;
    inline int       Backlog           = SOMAXCONN;
    inline int       MaxPendingAccept  = 512;
    inline bool      TcpNoDelay        = true;
    inline int       WorkerCount       = -1;
    inline bool      UsePolling        = true;
    inline uint32_t  CQSizePerWorker   = 65536;
    inline int       RecvOutstandingPerSession = 128;
    inline int       SendOutstandingPerSession = 128;
    inline uint32_t  SliceSize         = 1024;
    inline uint64_t  PoolBytes         = 64ull * 1024ull * 1024ull;
    inline int       PollBusySpinIters = 50;     // 200에서 50으로 감소 (CPU 사용량 감소)
    inline int       PollSleepMicros   = 1;      // 0에서 1로 변경 (짧은 Sleep 추가)
}


