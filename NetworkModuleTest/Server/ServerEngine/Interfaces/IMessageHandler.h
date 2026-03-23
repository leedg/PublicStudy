#pragma once

// 메시지 핸들러 추상 인터페이스.
// 각 서버(TestServer, DBServer)는 이 인터페이스를 상속하여 메시지 처리 로직을 구현한다.
// 기본 구현은 Implementations/Protocols/BaseMessageHandler를 참조.

#include "Message.h"
#include "MessageType_enum.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace Network::Interfaces
{

class IMessageHandler
{
  public:
	virtual ~IMessageHandler() = default;

	/**
	 * 수신 메시지를 파싱하고 등록된 핸들러에 디스패치한다.
	 * @param connectionId 송신측 연결 ID
	 * @param data         원시 메시지 데이터 (헤더 포함)
	 * @param size         데이터 크기 (바이트)
	 * @return 처리 성공 여부 (핸들러 미등록이면 false)
	 */
	virtual bool ProcessMessage(ConnectionId connectionId, const uint8_t *data,
								size_t size) = 0;

	/**
	 * 전송용 메시지를 직렬화한다.
	 * @param type         메시지 타입
	 * @param connectionId 수신측 연결 ID
	 * @param data         페이로드 (nullptr 허용 — 페이로드 없는 메시지)
	 * @param size         페이로드 크기
	 * @return 네트워크 전송 가능한 직렬화 바이트열
	 */
	virtual std::vector<uint8_t> CreateMessage(MessageType type,
												   ConnectionId connectionId,
												   const void *data,
												   size_t size) = 0;

	/**
	 * 현재 타임스탬프를 밀리초로 반환한다 (system_clock 기준).
	 */
	virtual uint64_t GetCurrentTimestamp() const = 0;

	/**
	 * 메시지 포맷이 유효한지 검사한다 (헤더 크기 및 dataSize 일관성 확인).
	 * @param data  원시 메시지 데이터
	 * @param size  데이터 크기
	 * @return 포맷이 유효하면 true
	 */
	virtual bool ValidateMessage(const uint8_t *data, size_t size) const = 0;
};

} // namespace Network::Interfaces
