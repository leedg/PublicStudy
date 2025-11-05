#pragma once
#include <windows.h>
#include <winsock2.h>
#include <MSWSock.h>
#include <vector>
#include <cstdint>
#include <mutex>

class BufferPool {
public:
    BufferPool();
    ~BufferPool();

    bool   init(uint64_t totalBytes, uint32_t sliceSize);
    void   cleanup();

    bool   allocSlice(uint32_t& offsetOut);
    void   freeSlice(uint32_t offset);

    RIO_BUFFERID bufferId() const { return m_bid; }
    char* base() const { return m_base; }
    uint32_t sliceSize() const { return m_sliceSize; }
    uint64_t totalBytes() const { return m_total; }

private:
    char*        m_base = nullptr;
    uint64_t     m_total = 0;
    RIO_BUFFERID m_bid = RIO_INVALID_BUFFERID;
    uint32_t     m_sliceSize = 0;
    uint32_t     m_slices = 0;
    std::vector<uint8_t> m_used;
    SRWLOCK      m_lock;
};
