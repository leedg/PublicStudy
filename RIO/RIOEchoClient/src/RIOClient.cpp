#include "RIOClient.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

inline uint32_t ToLittleEndian(uint32_t value) {
    return value;
}

inline uint32_t FromLittleEndian(uint32_t value) {
    return value;
}

} // namespace

RIOClient::RIOClient() = default;

RIOClient::~RIOClient() {
    cleanup();
}

bool RIOClient::ensureBuffers() {
    if (m_buffer) {
        return true;
    }

    m_buffer = static_cast<char*>(VirtualAlloc(NULL, kBufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!m_buffer) {
        printf("[CLIENT][ERROR] VirtualAlloc failed (error=%lu)\n", GetLastError());
        return false;
    }

    m_bufferId = RIONetwork::Rio().RIORegisterBuffer(m_buffer, kBufferSize);
    if (m_bufferId == RIO_INVALID_BUFFERID) {
        printf("[CLIENT][ERROR] RIORegisterBuffer failed (error=%lu)\n", GetLastError());
        return false;
    }

    return true;
}

void RIOClient::cleanup() {
    if (m_rq != RIO_INVALID_RQ) {
        // RIOCloseRequestQueue는 RIO API에 존재하지 않음
        // Request Queue는 socket이 닫히면 자동으로 해제됨
        m_rq = RIO_INVALID_RQ;
    }

    if (m_cq != RIO_INVALID_CQ) {
        RIONetwork::Rio().RIOCloseCompletionQueue(m_cq);
        m_cq = RIO_INVALID_CQ;
    }

    if (m_event) {
        CloseHandle(m_event);
        m_event = NULL;
    }

    if (m_bufferId != RIO_INVALID_BUFFERID) {
        RIONetwork::Rio().RIODeregisterBuffer(m_bufferId);
        m_bufferId = RIO_INVALID_BUFFERID;
    }

    if (m_buffer) {
        VirtualFree(m_buffer, 0, MEM_RELEASE);
        m_buffer = nullptr;
    }

    if (m_socket != INVALID_SOCKET) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }
}

bool RIOClient::connectTo(const std::string& host, uint16_t port) {
    cleanup();

    m_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_REGISTERED_IO | WSA_FLAG_OVERLAPPED);
    if (m_socket == INVALID_SOCKET) {
        printf("[CLIENT][ERROR] WSASocket failed (error=%d)\n", WSAGetLastError());
        return false;
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    char portStr[16];
    sprintf_s(portStr, "%hu", port);

    if (getaddrinfo(host.c_str(), portStr, &hints, &result) != 0) {
        printf("[CLIENT][ERROR] getaddrinfo failed (error=%d)\n", WSAGetLastError());
        return false;
    }

    bool connected = false;
    for (addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
        if (WSAConnect(m_socket, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen), NULL, NULL, NULL, NULL) == 0) {
            connected = true;
            break;
        }
    }

    freeaddrinfo(result);

    if (!connected) {
        printf("[CLIENT][ERROR] WSAConnect failed (error=%d)\n", WSAGetLastError());
        return false;
    }

    m_cq = RIONetwork::CreateEventCQ(64, m_event);
    if (m_cq == RIO_INVALID_CQ) {
        return false;
    }

    m_rq = RIONetwork::Rio().RIOCreateRequestQueue(
        m_socket,
        1, 1,
        1, 1,
        m_cq, m_cq,
        this);

    if (m_rq == RIO_INVALID_RQ) {
        printf("[CLIENT][ERROR] RIOCreateRequestQueue failed (error=%lu)\n", GetLastError());
        return false;
    }

    return ensureBuffers();
}

bool RIOClient::sendEcho(const rio::echo::EchoMessage& message) {
    if (!m_buffer) {
        printf("[CLIENT][ERROR] Buffer not initialized\n");
        return false;
    }

    std::string payload;
    if (!message.SerializeToString(&payload)) {
        printf("[CLIENT][ERROR] SerializeToString failed\n");
        return false;
    }

    const uint32_t totalLen = static_cast<uint32_t>(payload.size());
    const uint32_t packetLen = sizeof(uint32_t) + totalLen;

    if (packetLen > (kRecvOffset - kSendOffset)) {
        printf("[CLIENT][ERROR] Payload size exceeds send buffer\n");
        return false;
    }

    uint32_t* lengthPtr = reinterpret_cast<uint32_t*>(m_buffer + kSendOffset);
    *lengthPtr = ToLittleEndian(totalLen);
    std::memcpy(m_buffer + kSendOffset + sizeof(uint32_t), payload.data(), totalLen);

    RIO_BUF buf{};
    buf.BufferId = m_bufferId;
    buf.Length = packetLen;
    buf.Offset = kSendOffset;

    if (!RIONetwork::Rio().RIOSend(m_rq, &buf, 1, 0, &m_sendCtx)) {
        printf("[CLIENT][ERROR] RIOSend failed (error=%lu)\n", GetLastError());
        return false;
    }

    m_sendPending = true;
    return true;
}

bool RIOClient::receiveEcho(rio::echo::EchoMessage& message, DWORD timeoutMillis) {
    if (!m_buffer) {
        printf("[CLIENT][ERROR] Buffer not initialized\n");
        return false;
    }

    RIO_BUF buf{};
    buf.BufferId = m_bufferId;
    buf.Length = kBufferSize - kRecvOffset;
    buf.Offset = kRecvOffset;

    if (!RIONetwork::Rio().RIOReceive(m_rq, &buf, 1, 0, &m_recvCtx)) {
        printf("[CLIENT][ERROR] RIOReceive failed (error=%lu)\n", GetLastError());
        return false;
    }

    m_recvPending = true;

    bool parsed = false;

    while (m_sendPending || m_recvPending) {
        DWORD wait = WaitForSingleObject(m_event, timeoutMillis);
        if (wait != WAIT_OBJECT_0) {
            printf("[CLIENT][ERROR] WaitForSingleObject timeout or failure (code=%lu)\n", wait);
            return false;
        }

        RIORESULT results[8];
        ULONG count = RIONetwork::Rio().RIODequeueCompletion(m_cq, results, 8);
        if (count == RIO_CORRUPT_CQ) {
            printf("[CLIENT][ERROR] RIODequeueCompletion returned RIO_CORRUPT_CQ\n");
            return false;
        }

        if (count == 0) {
            continue;
        }

        for (ULONG i = 0; i < count; ++i) {
            const RIORESULT& res = results[i];
            auto* ctx = reinterpret_cast<RioContext*>(res.RequestContext);
            if (!ctx) {
                continue;
            }

            if (ctx->op == RioContext::Op::Send) {
                m_sendPending = false;
                if (res.Status != NO_ERROR) {
                    printf("[CLIENT][ERROR] Send completion error (status=%lu)\n", res.Status);
                    return false;
                }
            } else {
                m_recvPending = false;
                if (res.Status != NO_ERROR || res.BytesTransferred < sizeof(uint32_t)) {
                    printf("[CLIENT][ERROR] Receive completion error (status=%lu, bytes=%lu)\n", res.Status, res.BytesTransferred);
                    return false;
                }

                const uint8_t* base = reinterpret_cast<const uint8_t*>(m_buffer + kRecvOffset);
                uint32_t encodedLen = 0;
                std::memcpy(&encodedLen, base, sizeof(uint32_t));
                const uint32_t bodyLen = FromLittleEndian(encodedLen);

                if (bodyLen + sizeof(uint32_t) > res.BytesTransferred) {
                    printf("[CLIENT][ERROR] Incomplete payload (expected=%u, got=%lu)\n", bodyLen, res.BytesTransferred);
                    return false;
                }

                const uint8_t* payloadPtr = base + sizeof(uint32_t);
                if (!message.ParseFromArray(payloadPtr, static_cast<int>(bodyLen))) {
                    printf("[CLIENT][ERROR] ParseFromArray failed\n");
                    return false;
                }

                parsed = true;
            }
        }
    }

    return parsed;
}
