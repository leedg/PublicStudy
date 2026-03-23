#pragma once

// 네트워크 유틸리티용 공통 타입 및 상수 정의

#include <cstdint>
#include <cstddef>

namespace Network::Utils
{
// =============================================================================
// 타입 별칭
// =============================================================================

using NetworkHandle = uint64_t;
using ConnectionId  = uint64_t;
using MessageId     = uint32_t;
using BufferSize    = size_t;
using Timestamp     = uint64_t;

// =============================================================================
// 네트워크 상수
// =============================================================================

// 개발/테스트 환경 기본 포트. 실서비스 배포 시 CLI 인자로 재지정한다.
constexpr uint32_t DEFAULT_PORT = 8000;

// Windows와 Linux의 기본 포트를 분리한 이유:
//   Windows 개발 환경에서 1024 미만 포트는 권한이 필요하고,
//   실제 배포(Linux)와 포트 충돌을 피하기 위해 범위를 달리한다.
#if defined(_WIN32)
constexpr uint16_t DEFAULT_TEST_SERVER_PORT = 19010;
constexpr uint16_t DEFAULT_TEST_DB_PORT     = 18002;
#else
constexpr uint16_t DEFAULT_TEST_SERVER_PORT = 9000;
constexpr uint16_t DEFAULT_TEST_DB_PORT     = 8001;
#endif

// 단일 recv 버퍼 크기. 일반적인 게임 패킷(헤더 포함)이 4 KB 이하라는
// 가정에서 선택. 대형 패킷은 상위 레이어에서 재조립한다.
constexpr size_t DEFAULT_BUFFER_SIZE = 4096;

// 동시 접속 소프트 제한. 이 값을 초과해도 OS 레벨에서 연결은 수락되지만
// 세션 풀이 가득 찬 경우 연결을 거부하는 로직의 기준값으로 사용한다.
constexpr size_t MAX_CONNECTIONS = 1000;

// 비활성 세션을 정리하기 전에 대기하는 기본 타임아웃(밀리초).
// 30초는 대부분의 NAT keep-alive 주기보다 길어 조기 끊김을 방지한다.
constexpr int DEFAULT_TIMEOUT_MS = 30000;

constexpr Timestamp INVALID_TIMESTAMP = 0;

// Session::Send()용 소프트 백프레셔 임계값.
//   세션별 송신 큐가 이 깊이에 도달하면 Send()가 SendResult::QueueFull을
//   반환하여 호출자가 로그/드롭/종료 등으로 대응할 수 있도록 한다.
//   예상 버스트 크기와 메모리 예산에 맞게 조정할 것.
//   하드 한도(MAX_SEND_QUEUE_DEPTH = 1000)는 PacketDefine.h에 정의.
constexpr size_t SEND_QUEUE_BACKPRESSURE_THRESHOLD = 64;

// DB 큐 기본 워커 스레드 수.
//   배포 토폴로지에 맞게 CLI(-w 플래그)로 시작 시 재설정 가능.
constexpr size_t DEFAULT_DB_WORKER_COUNT         = 4; // OrderedTaskQueue (DBServer)
constexpr size_t DEFAULT_TASK_QUEUE_WORKER_COUNT = 3; // DBTaskQueue (TestServer) — 순서 보장 근거는 DBTaskQueue.h 참고

} // namespace Network::Utils
