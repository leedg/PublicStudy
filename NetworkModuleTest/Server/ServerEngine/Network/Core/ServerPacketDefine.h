#pragma once

// 서버 간 패킷 정의

#include <cstdint>
#include <ctime>

// 네트워크 패킷의 패딩을 비활성화하여 플랫폼 간 일관된 바이트 레이아웃 보장.
// 컴파일러 기본 정렬을 허용하면 필드 사이에 패딩이 삽입되어 직렬화가 깨진다.
#pragma pack(push, 1)

namespace Network::Core
{
    // =============================================================================
    // 서버 패킷 타입
    // ID 체계:
    //   0        : Invalid (미초기화 감지용)
    //   1000–1999: 서버 간 제어 (Ping/Pong)
    //   2000–2999: DB 요청/응답
    // =============================================================================

    enum class ServerPacketType : uint16_t
    {
        Invalid = 0,      // 미초기화 감지용 — 수신 시 프로토콜 오류로 처리

        // 서버 간 Ping/Pong (연결 상태 확인)
        ServerPingReq = 1000,
        ServerPongRes = 1001,

        // DB 요청/응답
        DBSavePingTimeReq = 2000,
        DBSavePingTimeRes = 2001,
        DBQueryReq        = 2002,
        DBQueryRes        = 2003,

        Max  // 범위 검사용 상한 (이 값 이상은 유효하지 않은 패킷)
    };

    // =============================================================================
    // 서버 패킷 헤더
    // =============================================================================

    struct ServerPacketHeader
    {
        uint16_t size;      // 패킷 전체 크기 (헤더 포함)
        uint16_t id;        // ServerPacketType 열거형 값
        uint32_t sequence;  // 요청/응답 매칭용 시퀀스 번호

        ServerPacketHeader()
            : size(sizeof(ServerPacketHeader))
            , id(static_cast<uint16_t>(ServerPacketType::Invalid))
            , sequence(0)
        {
        }

        template<typename T>
        void InitPacket()
        {
            size = sizeof(T);
            id = static_cast<uint16_t>(T::PacketId);
        }
    };

    // =============================================================================
    // 서버 Ping/Pong 패킷
    // =============================================================================

    struct PKT_ServerPingReq
    {
        static constexpr ServerPacketType PacketId = ServerPacketType::ServerPingReq;

        ServerPacketHeader header;
        uint64_t timestamp; // 송신 측 타임스탬프 (에포크 기준 밀리초, RTT 계산용)
        uint32_t sequence;  // 요청-응답 매칭용 시퀀스 번호

        PKT_ServerPingReq() : timestamp(0), sequence(0)
        {
            header.InitPacket<PKT_ServerPingReq>();
        }
    };

    struct PKT_ServerPongRes
    {
        static constexpr ServerPacketType PacketId = ServerPacketType::ServerPongRes;

        ServerPacketHeader header;
        uint64_t requestTimestamp;  // 원래 요청 타임스탬프 에코 (RTT 계산용)
        uint64_t responseTimestamp; // 서버 응답 타임스탬프 (에포크 기준 밀리초)
        uint32_t sequence;          // 요청 시퀀스 에코

        PKT_ServerPongRes() : requestTimestamp(0), responseTimestamp(0), sequence(0)
        {
            header.InitPacket<PKT_ServerPongRes>();
        }
    };

    // =============================================================================
    // DB Ping 시간 저장 패킷
    // =============================================================================

    struct PKT_DBSavePingTimeReq
    {
        static constexpr ServerPacketType PacketId = ServerPacketType::DBSavePingTimeReq;

        ServerPacketHeader header;
        uint32_t serverId;    // 서버 식별자
        uint64_t timestamp;   // Ping 타임스탬프 (GMT 기준 에포크 밀리초)
        char serverName[32];  // 서버 이름 (null 종료 문자열, 최대 31자)

        PKT_DBSavePingTimeReq() : serverId(0), timestamp(0)
        {
            header.InitPacket<PKT_DBSavePingTimeReq>();
            serverName[0] = '\0';
        }
    };

    struct PKT_DBSavePingTimeRes
    {
        static constexpr ServerPacketType PacketId = ServerPacketType::DBSavePingTimeRes;

        ServerPacketHeader header;
        uint32_t serverId; // 서버 식별자
        uint8_t  result;   // 0 = 성공, 0 이외 = 에러 코드
        char     message[64]; // 결과 메시지 (null 종료, 최대 63자)

        PKT_DBSavePingTimeRes() : serverId(0), result(0)
        {
            header.InitPacket<PKT_DBSavePingTimeRes>();
            message[0] = '\0';
        }
    };

    // =============================================================================
    // 범용 DB 쿼리 패킷
    // =============================================================================

    struct PKT_DBQueryReq
    {
        static constexpr ServerPacketType PacketId = ServerPacketType::DBQueryReq;

        ServerPacketHeader header;
        uint64_t queryId;     // 요청 ID (KeyGenerator::KeyId: tag|slot|seq48 구조)
        uint8_t  taskType;    // DBServerTaskType (uint8_t로 캐스팅)
        uint16_t dataLength;  // data[] 유효 바이트 수 (null 종료 문자 미포함)
        char     data[512];   // JSON 페이로드 (null 종료, dataLength 바이트 유효)
                              // 512 = 범용 쿼리 파라미터 최대 크기; 초과하면 DBQueryRes.result에 에러 반환)

        PKT_DBQueryReq() : queryId(0), taskType(0), dataLength(0)
        {
            header.InitPacket<PKT_DBQueryReq>();
            data[0] = '\0';
        }
    };

    struct PKT_DBQueryRes
    {
        static constexpr ServerPacketType PacketId = ServerPacketType::DBQueryRes;

        ServerPacketHeader header;
        uint64_t queryId;      // 요청 ID 에코 (요청-응답 매칭용)
        int32_t  result;       // Network::ResultCode (int32_t 캐스팅)
        uint16_t detailLength; // detail[] 유효 바이트 수
        char     detail[256];  // 상세 메시지 (null 종료, 최대 255자)

        PKT_DBQueryRes() : queryId(0), result(0), detailLength(0)
        {
            header.InitPacket<PKT_DBQueryRes>();
            detail[0] = '\0';
        }
    };

} // namespace Network::Core

// 기본 패킹으로 복원
#pragma pack(pop)
