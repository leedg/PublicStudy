# RIOEchoClient (VS2022)

- **설명**: RIO 기반 에코 서버(`RIOEchoServer`)에 접속하여 Google Protocol Buffers 형식의 메시지를 전송하고 동일한 패킷을 수신하는 클라이언트 예제입니다.
- **개발환경**: Visual Studio 2022, Windows 10/11, Winsock Registered I/O(RIO)

## 기능 요약
- RIO 함수 테이블 초기화 및 Request/Completion Queue 구성
- `EchoMessage` 프로토(Packet) 직렬화/역직렬화 (proto3 규약 준수)
- 메시지 전송 시 길이(4바이트 little-endian) + protobuf payload 구조 사용
- 서버에서 되돌려 준 패킷을 해석한 뒤 콘솔에 출력

## 빌드 방법
1. `RIOEchoClient.sln` 을 Visual Studio 2022에서 엽니다.
2. `x64 / Release` (또는 Debug) 구성으로 빌드합니다.

> 별도의 외부 라이브러리 없이 동작하도록 최소 protobuf 직렬화 로직을 포함했습니다.

## 실행 방법
```
RIOEchoClient.exe [host] [port] [message]
```
- 기본값: `127.0.0.1 5050 "Hello RIO"`
- 실행 전 `RIOEchoServer` 를 기동하면, 클라이언트가 접속하여 에코 응답을 확인할 수 있습니다.

## proto 정의
`proto/echo_message.proto`
```
syntax = "proto3";

package rio.echo;

message EchoMessage {
  string text = 1;
}
```
