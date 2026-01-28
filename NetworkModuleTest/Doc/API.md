# API 명세서

## 1. 개요

본 문서는 NetworkModuleTest 분산 서버 아키텍처의 API 인터페이스를 명세합니다.

## 2. 서버 API 목록

### 2.1 TestServer API (포트: 8001)

#### 2.1.1 클라이언트 연결
```
Endpoint: TCP 8001
Description: 클라이언트 연결 및 메시지 처리
Authentication: JWT Token Required
```

#### 2.1.2 핵심 API 목록

| API | Method | Description | Authentication |
|-----|--------|-------------|----------------|
| `/auth/login` | POST | 사용자 로그인 | None |
| `/auth/refresh` | POST | 토큰 갱신 | JWT |
| `/user/profile` | GET | 사용자 정보 조회 | JWT |
| `/user/data` | GET | 사용자 데이터 조회 | JWT |
| `/user/data` | POST | 사용자 데이터 생성 | JWT |
| `/user/data` | PUT | 사용자 데이터 수정 | JWT |
| `/user/data` | DELETE | 사용자 데이터 삭제 | JWT |
| `/ping` | GET | 서버 상태 확인 | JWT |

### 2.2 DBServer API (포트: 8002)

#### 2.2.1 서버간 통신
```
Endpoint: TCP 8002
Description: TestServer와의 내부 통신
Authentication: Server-to-Server Token
Access: Internal Only
```

#### 2.2.2 내부 API 목록

| API | Description | Access |
|-----|-------------|---------|
| `DB_QUERY` | 데이터 조회 | TestServer |
| `DB_INSERT` | 데이터 삽입 | TestServer |
| `DB_UPDATE` | 데이터 수정 | TestServer |
| `DB_DELETE` | 데이터 삭제 | TestServer |
| `DB_TRANSACTION` | 트랜잭션 관리 | TestServer |
| `HEALTH_CHECK` | 서버 상태 확인 | TestServer |

## 3. API 상세 명세

### 3.1 인증 API

#### 3.1.1 로그인
```
POST /auth/login
Content-Type: application/json
```

**Request:**
```json
{
    "user_id": "string",
    "password": "string",
    "client_info": {
        "ip": "string",
        "user_agent": "string"
    }
}
```

**Response:**
```json
{
    "success": true,
    "data": {
        "user_id": "string",
        "session_id": "string",
        "access_token": "string",
        "refresh_token": "string",
        "expires_at": 1640995200000,
        "permissions": [
            "read",
            "write",
            "delete"
        ]
    },
    "error": null
}
```

**Error Response:**
```json
{
    "success": false,
    "data": null,
    "error": {
        "code": 1001,
        "message": "Invalid credentials",
        "detail": "User ID or password is incorrect"
    }
}
```

#### 3.1.2 토큰 갱신
```
POST /auth/refresh
Content-Type: application/json
Authorization: Bearer {refresh_token}
```

**Request:**
```json
{
    "refresh_token": "string"
}
```

**Response:**
```json
{
    "success": true,
    "data": {
        "access_token": "string",
        "expires_at": 1640995200000
    }
}
```

### 3.2 사용자 데이터 API

#### 3.2.1 데이터 조회
```
GET /user/data?table={table}&id={id}&limit={limit}&offset={offset}
Authorization: Bearer {access_token}
```

**Query Parameters:**
- `table` (required): 조회할 테이블 이름
- `id` (optional): 데이터 ID
- `limit` (optional): 최대 개수 (default: 100)
- `offset` (optional): 시작 위치 (default: 0)

**Response:**
```json
{
    "success": true,
    "data": {
        "rows": [
            {
                "id": 1,
                "user_id": "testuser",
                "name": "Test User",
                "email": "test@example.com",
                "created_at": 1640995200000,
                "updated_at": 1640995200000
            }
        ],
        "total_count": 1,
        "limit": 100,
        "offset": 0
    }
}
```

#### 3.2.2 데이터 생성
```
POST /user/data
Content-Type: application/json
Authorization: Bearer {access_token}
```

**Request:**
```json
{
    "table": "users",
    "data": {
        "name": "New User",
        "email": "newuser@example.com",
        "phone": "123-456-7890"
    }
}
```

**Response:**
```json
{
    "success": true,
    "data": {
        "insert_id": 123,
        "affected_rows": 1
    }
}
```

#### 3.2.3 데이터 수정
```
PUT /user/data/{id}
Content-Type: application/json
Authorization: Bearer {access_token}
```

**Request:**
```json
{
    "table": "users",
    "data": {
        "name": "Updated Name",
        "email": "updated@example.com"
    }
}
```

**Response:**
```json
{
    "success": true,
    "data": {
        "affected_rows": 1
    }
}
```

#### 3.2.4 데이터 삭제
```
DELETE /user/data/{id}?table={table}
Authorization: Bearer {access_token}
```

**Response:**
```json
{
    "success": true,
    "data": {
        "affected_rows": 1
    }
}
```

### 3.3 상태 확인 API

#### 3.3.1 서버 상태
```
GET /ping
Authorization: Bearer {access_token}
```

**Response:**
```json
{
    "success": true,
    "data": {
        "server": "TestServer",
        "status": "healthy",
        "timestamp": 1640995200000,
        "uptime": 86400000,
        "version": "1.0.0",
        "active_connections": 150,
        "total_requests": 10000,
        "error_rate": 0.01
    }
}
```

## 4. DBServer 내부 API

### 4.1 데이터베이스 쿼리

#### 4.1.1 SELECT 요청
```protobuf
message DBQueryRequest {
    string table = 1;
    repeated string columns = 2;
    string where_clause = 3;
    map<string, string> parameters = 4;
    uint32 limit = 5;
    uint32 offset = 6;
    string order_by = 7;
}
```

**Response:**
```protobuf
message DBQueryResponse {
    bool success = 1;
    repeated map<string, string> rows = 2;
    uint32 total_count = 3;
    string error_message = 4;
}
```

#### 4.1.2 INSERT 요청
```protobuf
message DBInsertRequest {
    string table = 1;
    map<string, string> data = 2;
    bool return_id = 3;
}
```

**Response:**
```protobuf
message DBInsertResponse {
    bool success = 1;
    uint64 insert_id = 2;
    uint32 affected_rows = 3;
    string error_message = 4;
}
```

### 4.2 트랜잭션 관리

#### 4.2.1 트랜잭션 시작
```protobuf
message TransactionBegin {
    uint32 transaction_id = 1;
}
```

**Response:**
```protobuf
message TransactionResponse {
    bool success = 1;
    uint32 transaction_id = 2;
    string error_message = 3;
}
```

## 5. 에러 코드

### 5.1 HTTP 상태 코드
| Code | Description | When to Use |
|------|-------------|-------------|
| 200 | Success | Request processed successfully |
| 201 | Created | Resource created successfully |
| 400 | Bad Request | Invalid request format |
| 401 | Unauthorized | Authentication failed |
| 403 | Forbidden | Permission denied |
| 404 | Not Found | Resource not found |
| 409 | Conflict | Resource conflict |
| 429 | Too Many Requests | Rate limit exceeded |
| 500 | Internal Server Error | Server error |
| 503 | Service Unavailable | Service temporarily unavailable |

### 5.2 비즈니스 에러 코드
| Code | Description |
|------|-------------|
| 1000 | Invalid Request |
| 1001 | Authentication Failed |
| 1002 | Permission Denied |
| 1003 | Resource Not Found |
| 1004 | Resource Conflict |
| 1005 | Rate Limit Exceeded |
| 2000 | Database Error |
| 2001 | Connection Failed |
| 2002 | Query Timeout |
| 2003 | Transaction Failed |

## 6. 인증 및 보안

### 6.1 JWT Token 구조
```json
{
    "header": {
        "alg": "HS256",
        "typ": "JWT"
    },
    "payload": {
        "user_id": "string",
        "session_id": "string",
        "permissions": ["read", "write"],
        "iat": 1640995200,
        "exp": 1640998800
    }
}
```

### 6.2 권한 목록
| Permission | Description | Level |
|------------|-------------|-------|
| read | 데이터 조회 권한 | 1 |
| write | 데이터 생성/수정 권한 | 2 |
| delete | 데이터 삭제 권한 | 4 |
| admin | 관리자 권한 | 8 |

### 6.3 보안 정책
- **암호화**: TLS 1.3+ 통신
- **토큰 만료**: Access Token 1시간, Refresh Token 30일
- **비밀번호**: 최소 8자, 영문+숫자+특수문자
- **Rate Limit**: 분당 1000요청
- **IP 제한**: 실패 5회시 1분 차단

## 7. 데이터 모델

### 7.1 사용자 테이블 (users)
| Column | Type | Description |
|--------|------|-------------|
| id | BIGINT | Primary Key |
| user_id | VARCHAR(50) | 사용자 ID |
| password_hash | VARCHAR(255) | 비밀번호 해시 |
| name | VARCHAR(100) | 사용자 이름 |
| email | VARCHAR(255) | 이메일 |
| phone | VARCHAR(20) | 전화번호 |
| status | TINYINT | 상태 (1:활성, 0:비활성) |
| created_at | TIMESTAMP | 생성 시간 |
| updated_at | TIMESTAMP | 수정 시간 |

### 7.2 세션 테이블 (sessions)
| Column | Type | Description |
|--------|------|-------------|
| id | BIGINT | Primary Key |
| user_id | VARCHAR(50) | 사용자 ID |
| session_id | VARCHAR(255) | 세션 ID |
| refresh_token | VARCHAR(500) | 리프레시 토큰 |
| expires_at | TIMESTAMP | 만료 시간 |
| created_at | TIMESTAMP | 생성 시간 |

## 8. 사용 예제

### 8.1 전체 로그인 흐름
```bash
# 1. 로그인 요청
curl -X POST http://localhost:8001/auth/login \
  -H "Content-Type: application/json" \
  -d '{
    "user_id": "testuser",
    "password": "password123"
  }'

# 응답에서 access_token 추출
ACCESS_TOKEN="eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..."

# 2. 데이터 조회
curl -X GET "http://localhost:8001/user/data?table=users&limit=10" \
  -H "Authorization: Bearer $ACCESS_TOKEN"

# 3. 데이터 생성
curl -X POST http://localhost:8001/user/data \
  -H "Authorization: Bearer $ACCESS_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{
    "table": "test_data",
    "data": {
      "title": "Test Item",
      "content": "Test content"
    }
  }'
```

### 8.2 에러 처리 예제
```bash
# 인증 실패
curl -X GET http://localhost:8001/user/data \
  -H "Authorization: Bearer invalid_token"

# 응답
{
    "success": false,
    "data": null,
    "error": {
        "code": 401,
        "message": "Unauthorized",
        "detail": "Invalid or expired token"
    }
}
```

---

*본 API 명세는 구현 과정에서 상세화되고 업데이트됩니다.*