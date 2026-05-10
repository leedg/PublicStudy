#pragma once

// 메시지 타입 열거형.
//
// 값 범위 설계 의도:
//   0        : Unknown — 초기화되지 않았거나 파싱 실패를 나타내는 sentinel.
//   1 ~ 999  : 시스템 예약 — 엔진 레벨에서 사용하는 공통 메시지 (Ping/Pong 등).
//   1000 ~   : CustomStart — 각 서버(TestServer, DBServer)가 자유롭게 확장하는 영역.
//
// 통신 방향:
//   Ping : Client → TestServer (연결 생존 확인 요청)
//   Pong : TestServer → Client (Ping에 대한 응답)
//
// 확장 방법:
//   서버별 헤더에서 MessageType(uint32_t)을 직접 캐스팅하여 CustomStart 이후 값을 정의한다.
//   예: enum class DBMessageType : uint32_t { QueryRequest = 1000, QueryResponse = 1001 };

#include <cstdint>

namespace Network::Interfaces
{

enum class MessageType : uint32_t
{
	Unknown     = 0,    // 파싱 실패 / 미초기화 sentinel
	Ping        = 1,    // Client → Server 생존 확인 요청
	Pong        = 2,    // Server → Client 응답
	CustomStart = 1000  // 서버별 확장 시작 값
};

} // namespace Network::Interfaces
