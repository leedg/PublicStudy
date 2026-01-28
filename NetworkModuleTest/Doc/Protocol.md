# 서버 통신 프로토콜 명세

## 1. 개요

본 문서는 NetworkModuleTest 분산 서버 아키텍처의 서버간 통신 프로토콜을 명세합니다.

## 2. 프로토콜 기본

### 2.1 전송 계층
- **기반**: TCP/IP
- **직렬화**: Protocol Buffers v3
- **압축**: 선택적 (gzip/zstd)
- **암호화**: 선택적 (TLS 1.3)

### 2.2 메시지 구조
```protobuf
syntax = "proto3";

package Network.Protocol;

// 기본 메시지 헤더
message MessageHeader {
    uint32 message_id = 1;      // 메시지 고유 ID
    MessageType type = 2;       // 메시지 타입
    uint64 timestamp = 3;       // 생성 시간 (Unix epoch ms)
    uint32 source_server = 4;   // 송신 서버 ID
    uint32 target_server = 5;   // 수신 서버 ID
    uint32 sequence = 6;        // 시퀀스 번호 (순서 보장용)
}

// 전체 메시지
message NetworkMessage {
    MessageHeader header = 1;
    bytes payload = 2;          // 실제 데이터 (각 메시지 타입별 직렬화)
}
```

### 2.3 메시지 타입
```protobuf
enum MessageType {
    // 기본 통신
    PING = 1;
    PONG = 2;
    
    // 데이터베이스 요청
    DB_QUERY_REQUEST = 10;
    DB_QUERY_RESPONSE = 11;
    DB_INSERT_REQUEST = 12;
    DB_INSERT_RESPONSE = 13;
    DB_UPDATE_REQUEST = 14;
    DB_UPDATE_RESPONSE = 15;
    DB_DELETE_REQUEST = 16;
    DB_DELETE_RESPONSE = 17;
    DB_TRANSACTION_BEGIN = 18;
    DB_TRANSACTION_COMMIT = 19;
    DB_TRANSACTION_ROLLBACK = 20;
    
    // 인증 및 권한
    AUTH_REQUEST = 30;
    AUTH_RESPONSE = 31;
    PERMISSION_CHECK = 32;
    PERMISSION_RESPONSE = 33;
    
    // 에러 처리
    ERROR_RESPONSE = 99;
}
```

## 3. 핵심 프로토콜 상세

### 3.1 Ping/Pong (서버 상태 확인)
```protobuf
// Ping 요청
message PingRequest {
    string message = 1;         // 선택적 메시지
    uint64 timestamp = 2;       // 요청 시간
    map<string, string> metadata = 3; // 추가 정보
}

// Pong 응답
message PongResponse {
    string message = 1;         // 응답 메시지
    uint64 ping_timestamp = 2;  // 원본 ping 시간
    uint64 pong_timestamp = 3;  // 응답 시간
    map<string, string> server_info = 4; // 서버 정보
}
```

### 3.2 데이터베이스 CRUD

#### 조회 (SELECT)
```protobuf
message DBQueryRequest {
    string table = 1;           // 테이블 이름
    repeated string columns = 2; // 조회 컬럼
    string where_clause = 3;    // WHERE 조건
    map<string, string> parameters = 4; // 파라미터
    uint32 limit = 5;          // LIMIT
    uint32 offset = 6;         // OFFSET
    string order_by = 7;       // ORDER BY
}

message DBQueryResponse {
    bool success = 1;
    repeated map<string, string> rows = 2; // 결과 데이터
    uint32 total_count = 3;    // 전체 개수
    string error_message = 4;  // 에러 메시지
}
```

#### 삽입 (INSERT)
```protobuf
message DBInsertRequest {
    string table = 1;
    map<string, string> data = 2; // 삽입 데이터
    bool return_id = 3;        // ID 반환 여부
}

message DBInsertResponse {
    bool success = 1;
    uint64 insert_id = 2;      // 삽입된 ID
    uint32 affected_rows = 3;  // 영향받은 행 수
    string error_message = 4;
}
```

#### 수정 (UPDATE)
```protobuf
message DBUpdateRequest {
    string table = 1;
    map<string, string> data = 2;  // 수정 데이터
    string where_clause = 3;        // WHERE 조건
    map<string, string> parameters = 4;
}

message DBUpdateResponse {
    bool success = 1;
    uint32 affected_rows = 2;  // 영향받은 행 수
    string error_message = 3;
}
```

#### 삭제 (DELETE)
```protobuf
message DBDeleteRequest {
    string table = 1;
    string where_clause = 2;
    map<string, string> parameters = 3;
}

message DBDeleteResponse {
    bool success = 1;
    uint32 affected_rows = 2;
    string error_message = 3;
}
```

### 3.3 트랜잭션 관리
```protobuf
message TransactionBegin {
    uint32 transaction_id = 1;
}

message TransactionResponse {
    bool success = 1;
    uint32 transaction_id = 2;
    string error_message = 3;
}

message TransactionCommit {
    uint32 transaction_id = 1;
}

message TransactionRollback {
    uint32 transaction_id = 1;
    string reason = 2;
}
```

### 3.4 인증 프로토콜
```protobuf
message AuthRequest {
    string token = 1;           // JWT 토큰
    string client_id = 2;       // 클라이언트 ID
    uint32 permissions = 3;     // 요청 권한 비트마스크
}

message AuthResponse {
    bool success = 1;
    uint32 user_id = 2;
    string session_id = 3;
    uint32 permissions = 4;     // 부여된 권한
    uint64 expires_at = 5;      // 만료 시간
    string error_message = 6;
}
```

## 4. 통신 흐름

### 4.1 기본 요청-응답 흐름
```
TestServer                    DBServer
    |                           |
    |--- DB_QUERY_REQUEST ----->|
    |                           |
    |<-- DB_QUERY_RESPONSE -----|
    |                           |
```

### 4.2 트랜잭션 흐름
```
TestServer                    DBServer
    |                           |
    |--- TRANSACTION_BEGIN --->|
    |                           |
    |<-- TRANSACTION_RESPONSE --|
    |                           |
    |--- DB_INSERT_REQUEST ---->|
    |                           |
    |<-- DB_INSERT_RESPONSE ----|
    |                           |
    |--- DB_UPDATE_REQUEST ---->|
    |                           |
    |<-- DB_UPDATE_RESPONSE ----|
    |                           |
    |--- TRANSACTION_COMMIT --->|
    |                           |
    |<-- TRANSACTION_RESPONSE --|
    |                           |
```

### 4.3 인증 흐름
```
TestServer                    DBServer
    |                           |
    |--- AUTH_REQUEST -------->|
    |                           |
    |<-- AUTH_RESPONSE --------|
    |                           |
```

## 5. 에러 처리

### 5.1 에러 형식
```protobuf
enum ErrorCode {
    SUCCESS = 0;
    INVALID_REQUEST = 1000;
    UNAUTHORIZED = 1001;
    FORBIDDEN = 1002;
    NOT_FOUND = 1003;
    SERVER_ERROR = 2000;
    DATABASE_ERROR = 2001;
    TIMEOUT_ERROR = 2002;
}

message ErrorResponse {
    ErrorCode code = 1;
    string message = 2;
    string detail = 3;
    uint64 timestamp = 4;
    string request_id = 5;
}
```

### 5.2 에러 처리 규칙
- **클라이언트 오류**: 1000번대 (4xx)
- **서버 오류**: 2000번대 (5xx)
- **재시도 가능**: 타임아웃, 일시적 서버 오류
- **즉시 중단**: 인증 오류, 잘못된 요청

## 6. 성능 최적화

### 6.1 커넥션 관리
- **커넥션 풀**: 최대 10개 커넥션 유지
- **Keep-Alive**: 30초 타임아웃
- **재사용**: 요청마다 커넥션 재사용

### 6.2 메시지 최적화
- **배치 처리**: 여러 요청을 하나로 묶어 전송
- **압축**: 큰 메시지는 자동 압축
- **캐싱**: 반복 쿼리 결과 캐싱

### 6.3 타임아웃 설정
- **연결 타임아웃**: 5초
- **요청 타임아웃**: 30초
- **읽기 타임아웃**: 10초
- **쓰기 타임아웃**: 10초

## 7. 보안

### 7.1 암호화
- **전송**: TLS 1.3 또는 그 이상
- **데이터**: 민감 정보는 별도 암호화
- **키 관리**: 주기적 키 로테이션

### 7.2 인증
- **토큰**: JWT (JSON Web Token)
- **만료**: 1시간 기본 만료
- **갱신**: Refresh Token 지원

### 7.3 권한
- **롤 기반**: Role-Based Access Control (RBAC)
- **리소스**: 리소스별 권한 관리
- **감사**: 모든 접근 로그 기록

---

*본 프로토콜 명세는 구현 과정에서 상세화되고 업데이트됩니다.*