#include "RIOClient.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{

// Little Endian으로 변환 (x86/x64 아키텍처에서는 변환 불필요)
inline uint32_t ToLittleEndian(uint32_t value)
{
    return value;
}

// Little Endian에서 호스트 바이트 순서로 변환 (x86/x64 아키텍처에서는 변환 불필요)
inline uint32_t FromLittleEndian(uint32_t value)
{
    return value;
}

} // namespace

// 기본 생성자
RIOClient::RIOClient() = default;

// 소멸자 - 모든 리소스 정리
RIOClient::~RIOClient()
{
    cleanup();
}

/**
 * @brief RIO 버퍼를 할당하고 등록하는 함수
 * @return 성공 시 true, 실패 시 false
 * 
 * - VirtualAlloc을 사용하여 4KB 크기의 버퍼 할당
 * - RIORegisterBuffer를 호출하여 RIO에 버퍼 등록
 * - 버퍼는 송신/수신 영역으로 분할하여 사용 (0~2KB: 송신, 2KB~4KB: 수신)
 */
bool RIOClient::ensureBuffers()
{
    // 이미 버퍼가 할당되어 있으면 성공 반환
    if (m_buffer)
    {
        return true;
    }

    // VirtualAlloc으로 메모리 할당 (페이지 정렬된 메모리 필요)
    m_buffer = static_cast<char*>(VirtualAlloc(NULL, kBufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!m_buffer)
    {
        printf("[CLIENT][ERROR] VirtualAlloc failed (error=%lu)\n", GetLastError());
        return false;
    }

    // RIO에 버퍼 등록 - 등록된 버퍼만 RIO API에서 사용 가능
    m_bufferId = RIONetwork::Rio().RIORegisterBuffer(m_buffer, kBufferSize);
    if (m_bufferId == RIO_INVALID_BUFFERID)
    {
        printf("[CLIENT][ERROR] RIORegisterBuffer failed (error=%lu)\n", GetLastError());
        return false;
    }

    return true;
}

/**
 * @brief 모든 RIO 리소스와 소켓을 정리하는 함수
 * 
 * 정리 순서:
 * 1. Request Queue (자동 해제됨)
 * 2. Completion Queue
 * 3. Event 핸들
 * 4. RIO 버퍼
 * 5. 메모리 버퍼
 * 6. 소켓
 */
void RIOClient::cleanup()
{
    // Request Queue는 명시적 close 함수가 없음 - 소켓 닫힐 때 자동 해제
    if (m_rq != RIO_INVALID_RQ)
    {
        // RIOCloseRequestQueue는 RIO API에 존재하지 않음
        // Request Queue는 소켓이 닫힐 때 자동으로 해제됨
        m_rq = RIO_INVALID_RQ;
    }

    // Completion Queue 닫기
    if (m_cq != RIO_INVALID_CQ)
    {
        RIONetwork::Rio().RIOCloseCompletionQueue(m_cq);
        m_cq = RIO_INVALID_CQ;
    }

    // 이벤트 핸들 닫기
    if (m_event)
    {
        CloseHandle(m_event);
        m_event = NULL;
    }

    // RIO 버퍼 등록 해제
    if (m_bufferId != RIO_INVALID_BUFFERID)
    {
        RIONetwork::Rio().RIODeregisterBuffer(m_bufferId);
        m_bufferId = RIO_INVALID_BUFFERID;
    }

    // 버퍼 메모리 해제
    if (m_buffer)
    {
        VirtualFree(m_buffer, 0, MEM_RELEASE);
        m_buffer = nullptr;
    }

    // 소켓 닫기
    if (m_socket != INVALID_SOCKET)
    {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }
}

/**
 * @brief 서버에 연결하는 함수
 * @param host 서버 호스트명 또는 IP 주소
 * @param port 서버 포트 번호
 * @return 성공 시 true, 실패 시 false
 * 
 * 연결 순서:
 * 1. 이전 리소스 정리
 * 2. RIO 소켓 생성 (WSA_FLAG_REGISTERED_IO 플래그 필수)
 * 3. getaddrinfo로 주소 정보 조회
 * 4. WSAConnect로 서버에 연결
 * 5. Completion Queue 생성
 * 6. Request Queue 생성
 * 7. 버퍼 할당 및 등록
 */
bool RIOClient::connectTo(const std::string& host, uint16_t port)
{
    // 기존 연결이 있다면 먼저 정리
    cleanup();

    // RIO를 사용하기 위해 WSA_FLAG_REGISTERED_IO 플래그로 소켓 생성
    m_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_REGISTERED_IO | WSA_FLAG_OVERLAPPED);
    if (m_socket == INVALID_SOCKET)
    {
        printf("[CLIENT][ERROR] WSASocket failed (error=%d)\n", WSAGetLastError());
        return false;
    }

    // 주소 정보 설정
    addrinfo hints{};
    hints.ai_family = AF_INET;      // IPv4
    hints.ai_socktype = SOCK_STREAM; // TCP
    hints.ai_protocol = IPPROTO_TCP;

    // 호스트명/IP를 주소 정보로 변환
    addrinfo* result = nullptr;
    char portStr[16];
    sprintf_s(portStr, "%hu", port);

    if (getaddrinfo(host.c_str(), portStr, &hints, &result) != 0)
    {
        printf("[CLIENT][ERROR] getaddrinfo failed (error=%d)\n", WSAGetLastError());
        return false;
    }

    // 반환된 주소 목록을 순회하며 연결 시도
    bool connected = false;
    for (addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next)
    {
        if (WSAConnect(m_socket, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen), NULL, NULL, NULL, NULL) == 0)
        {
            connected = true;
            break;
        }
    }

    freeaddrinfo(result);

    if (!connected)
    {
        printf("[CLIENT][ERROR] WSAConnect failed (error=%d)\n", WSAGetLastError());
        return false;
    }

    // 완료 알림을 받기 위한 Completion Queue 생성 (이벤트 기반)
    m_cq = RIONetwork::CreateEventCQ(64, m_event);
    if (m_cq == RIO_INVALID_CQ)
    {
        return false;
    }

    // 송신/수신 요청을 처리하기 위한 Request Queue 생성
    // 파라미터: 소켓, 최대 수신 요청 수, 최대 수신 버퍼 수, 최대 송신 요청 수, 최대 송신 버퍼 수,
    //          수신 CQ, 송신 CQ, 컨텍스트 포인터
    m_rq = RIONetwork::Rio().RIOCreateRequestQueue(
        m_socket,
        1, 1,  // 수신: 동시 요청 1개, 버퍼 1개
        1, 1,  // 송신: 동시 요청 1개, 버퍼 1개
        m_cq, m_cq,  // 수신/송신 모두 같은 CQ 사용
        this);  // 컨텍스트 포인터 (사용 안 함)

    if (m_rq == RIO_INVALID_RQ)
    {
        printf("[CLIENT][ERROR] RIOCreateRequestQueue failed (error=%lu)\n", GetLastError());
        return false;
    }

    // 송수신에 사용할 버퍼 할당 및 등록
    return ensureBuffers();
}

/**
 * @brief Echo 메시지를 서버로 전송하는 함수
 * @param message 전송할 Protobuf 메시지
 * @return 성공 시 true, 실패 시 false
 * 
 * 전송 프로토콜:
 * [4바이트 길이][N바이트 Protobuf 데이터]
 * - 길이는 Little Endian uint32_t
 * - Protobuf 데이터는 직렬화된 메시지
 */
bool RIOClient::sendEcho(const rio::echo::EchoMessage& message)
{
    // 상태 검증
    if (!m_buffer)
    {
        printf("[CLIENT][ERROR] Buffer not initialized\n");
        return false;
    }

    if (m_socket == INVALID_SOCKET)
    {
        printf("[CLIENT][ERROR] Socket is not connected\n");
        return false;
    }

    if (m_rq == RIO_INVALID_RQ)
    {
        printf("[CLIENT][ERROR] Request queue is invalid\n");
        return false;
    }

    // 이전 송신이 완료되지 않았으면 에러
    if (m_sendPending)
    {
        printf("[CLIENT][ERROR] Previous send operation still pending\n");
        return false;
    }

    // Protobuf 메시지를 바이트 배열로 직렬화
    std::string payload;
    if (!message.SerializeToString(&payload))
    {
        printf("[CLIENT][ERROR] SerializeToString failed\n");
        return false;
    }

    printf("[CLIENT] Serialized message: %zu bytes (text: %s)\n", payload.size(), message.text().c_str());

    // 패킷 구조: [4바이트 길이 헤더][페이로드]
    const uint32_t totalLen = static_cast<uint32_t>(payload.size());
    const uint32_t packetLen = sizeof(uint32_t) + totalLen;

    // 송신 버퍼 크기 검증 (0~2048 바이트 영역 사용)
    if (packetLen > (kRecvOffset - kSendOffset))
    {
        printf("[CLIENT][ERROR] Payload size exceeds send buffer (size=%u, max=%u)\n", 
               packetLen, kRecvOffset - kSendOffset);
        return false;
    }

    // 버퍼에 데이터 복사: [길이 헤더][페이로드]
    uint32_t* lengthPtr = reinterpret_cast<uint32_t*>(m_buffer + kSendOffset);
    *lengthPtr = ToLittleEndian(totalLen);  // 길이 헤더 쓰기
    std::memcpy(m_buffer + kSendOffset + sizeof(uint32_t), payload.data(), totalLen);  // 페이로드 쓰기

    printf("[CLIENT] Prepared packet: %u bytes (header=4, payload=%u)\n", packetLen, totalLen);

    // RIO 버퍼 디스크립터 설정
    RIO_BUF buf{};
    buf.BufferId = m_bufferId;  // 등록된 버퍼 ID
    buf.Length = packetLen;     // 전송할 데이터 길이
    buf.Offset = kSendOffset;   // 버퍼 내 오프셋 (0부터 시작)

    // 비동기 송신 요청 - 완료는 Completion Queue를 통해 알림받음
    if (!RIONetwork::Rio().RIOSend(m_rq, &buf, 1, 0, &m_sendCtx))
    {
        DWORD error = GetLastError();
        printf("[CLIENT][ERROR] RIOSend failed (error=%lu)\n", error);
        return false;
    }

    // 송신 완료 대기 플래그 설정
    m_sendPending = true;
    printf("[CLIENT] RIOSend posted successfully\n");
    return true;
}

/**
 * @brief Echo 메시지를 서버로부터 수신하는 함수
 * @param message 수신된 데이터를 저장할 Protobuf 메시지
 * @param timeoutMillis 타임아웃 (밀리초)
 * @return 성공 시 true, 실패 시 false
 * 
 * 수신 프로토콜:
 * [4바이트 길이][N바이트 Protobuf 데이터]
 * 
 * 동작 방식:
 * 1. RIOReceive로 비동기 수신 요청
 * 2. WaitForSingleObject로 완료 대기
 * 3. RIODequeueCompletion으로 완료 결과 조회
 * 4. 송신/수신 완료 처리
 * 5. 수신 데이터 파싱
 */
bool RIOClient::receiveEcho(rio::echo::EchoMessage& message, DWORD timeoutMillis)
{
    // 상태 검증
    if (!m_buffer)
    {
        printf("[CLIENT][ERROR] Buffer not initialized\n");
        return false;
    }

    if (m_socket == INVALID_SOCKET)
    {
        printf("[CLIENT][ERROR] Socket is not connected\n");
        return false;
    }

    if (m_rq == RIO_INVALID_RQ)
    {
        printf("[CLIENT][ERROR] Request queue is invalid\n");
        return false;
    }

    if (m_cq == RIO_INVALID_CQ)
    {
        printf("[CLIENT][ERROR] Completion queue is invalid\n");
        return false;
    }

    if (m_event == NULL)
    {
        printf("[CLIENT][ERROR] Event handle is null\n");
        return false;
    }

    // RIO 버퍼 디스크립터 설정 (수신 버퍼 영역: 2048~4096)
    RIO_BUF buf{};
    buf.BufferId = m_bufferId;
    buf.Length = kBufferSize - kRecvOffset;  // 2048 바이트
    buf.Offset = kRecvOffset;                 // 오프셋 2048

    // 비동기 수신 요청
    if (!RIONetwork::Rio().RIOReceive(m_rq, &buf, 1, 0, &m_recvCtx))
    {
        printf("[CLIENT][ERROR] RIOReceive failed (error=%lu)\n", GetLastError());
        return false;
    }

    m_recvPending = true;
    printf("[CLIENT] RIOReceive posted, waiting for completion...\n");

    bool parsed = false;
    int loopCount = 0;
    const int maxLoops = 1000; // 무한 루프 방지

    // 송신과 수신이 모두 완료될 때까지 대기
    while (m_sendPending || m_recvPending)
    {
        // 무한 루프 방지
        if (++loopCount > maxLoops)
        {
            printf("[CLIENT][ERROR] Exceeded maximum wait loops\n");
            return false;
        }

        // 이벤트가 시그널될 때까지 대기 (Completion Queue에 결과가 있음을 의미)
        DWORD wait = WaitForSingleObject(m_event, timeoutMillis);
        if (wait == WAIT_TIMEOUT)
        {
            printf("[CLIENT][ERROR] WaitForSingleObject timeout (sendPending=%d, recvPending=%d)\n",
                   m_sendPending, m_recvPending);
            return false;
        }
        if (wait != WAIT_OBJECT_0)
        {
            printf("[CLIENT][ERROR] WaitForSingleObject failed (code=%lu, error=%lu)\n", wait, GetLastError());
            return false;
        }

        // Completion Queue에서 완료 결과 조회 (최대 8개)
        RIORESULT results[8];
        ULONG count = RIONetwork::Rio().RIODequeueCompletion(m_cq, results, 8);
        if (count == RIO_CORRUPT_CQ)
        {
            printf("[CLIENT][ERROR] RIODequeueCompletion returned RIO_CORRUPT_CQ\n");
            return false;
        }

        if (count == 0)
        {
            printf("[CLIENT][WARNING] RIODequeueCompletion returned 0 results\n");
            continue;
        }

        printf("[CLIENT] Processing %lu completion(s)\n", count);

        // 각 완료 결과 처리
        for (ULONG i = 0; i < count; ++i)
        {
            const RIORESULT& res = results[i];

            // RequestContext가 NULL인 경우 (비정상)
            if (res.RequestContext == 0)
            {
                printf("[CLIENT][WARNING] Null RequestContext at index %lu\n", i);
                continue;
            }

            // RequestContext를 RioContext 포인터로 변환
            auto* ctx = reinterpret_cast<RioContext*>(res.RequestContext);
            if (!ctx)
            {
                printf("[CLIENT][ERROR] Failed to cast RequestContext\n");
                continue;
            }

            // 송신 완료 처리
            if (ctx->op == RioContext::Op::Send)
            {
                m_sendPending = false;
                if (res.Status != NO_ERROR)
                {
                    printf("[CLIENT][ERROR] Send completion error (status=%lu)\n", res.Status);
                    return false;
                }
                printf("[CLIENT] Send completed (%lu bytes)\n", res.BytesTransferred);
            }
            // 수신 완료 처리
            else
            {
                m_recvPending = false;
                if (res.Status != NO_ERROR)
                {
                    printf("[CLIENT][ERROR] Receive completion error (status=%lu, bytes=%lu)\n", res.Status, res.BytesTransferred);
                    return false;
                }

                // 최소한 4바이트 (길이 헤더)는 있어야 함
                if (res.BytesTransferred < sizeof(uint32_t))
                {
                    printf("[CLIENT][ERROR] Received data too small (bytes=%lu)\n", res.BytesTransferred);
                    return false;
                }

                printf("[CLIENT] Received %lu bytes\n", res.BytesTransferred);

                // 수신 버퍼에서 데이터 읽기
                const uint8_t* base = reinterpret_cast<const uint8_t*>(m_buffer + kRecvOffset);
                
                // 길이 헤더 읽기 (첫 4바이트)
                uint32_t encodedLen = 0;
                std::memcpy(&encodedLen, base, sizeof(uint32_t));
                const uint32_t bodyLen = FromLittleEndian(encodedLen);

                printf("[CLIENT] Payload length: %u bytes\n", bodyLen);

                // 전체 데이터 크기 검증 (헤더 + 페이로드)
                if (bodyLen + sizeof(uint32_t) > res.BytesTransferred)
                {
                    printf("[CLIENT][ERROR] Incomplete payload (expected=%u, got=%lu)\n", bodyLen, res.BytesTransferred);
                    return false;
                }

                // 버퍼 오버플로우 방지
                if (bodyLen > kBufferSize)
                {
                    printf("[CLIENT][ERROR] Payload too large (size=%u, max=%u)\n", bodyLen, kBufferSize);
                    return false;
                }

                // 페이로드 부분을 Protobuf 메시지로 파싱
                const uint8_t* payloadPtr = base + sizeof(uint32_t);
                if (!message.ParseFromArray(payloadPtr, static_cast<int>(bodyLen)))
                {
                    printf("[CLIENT][ERROR] ParseFromArray failed\n");
                    return false;
                }

                printf("[CLIENT] Parsed message: %s\n", message.text().c_str());

                parsed = true;
            }
        }
    }

    // 메시지가 파싱되지 않았으면 실패
    if (!parsed)
    {
        printf("[CLIENT][ERROR] Message was not parsed\n");
        return false;
    }

    return parsed;
}
