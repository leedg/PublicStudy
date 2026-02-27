# TestServer Naming Conventions

> **적용 범위**: 이 문서는 **TestServer 프로젝트** 한정 네이밍 규칙을 다룹니다.
> 전체 프로젝트 코딩 스타일은 `Doc/Network/CodingConventions.md`를 참조하세요.

## 개요

TestServer는 명시적이고 자기설명적인(self-descriptive) 네이밍 규칙을 따릅니다.

## 클래스 네이밍

### 패턴: `TestServer` + `기능` + `역할`

#### 예시

**TestServerMessageHandler**
- `TestServer`: 이 서버 전용 구현임을 명시
- `Message`: 메시지 관련 기능
- `Handler`: 처리기 역할

**TestServerDatabaseManager**
- `TestServer`: 이 서버 전용 구현임을 명시
- `Database`: 데이터베이스 관련 기능
- `Manager`: 관리자 역할

### 왜 이런 네이밍을 사용하는가?

1. **명확성**: 클래스 이름만으로 무엇을 하는지 알 수 있음
2. **검색 용이성**: IDE에서 "TestServer"로 시작하면 모든 서버 구현 찾기 가능
3. **충돌 방지**: 다른 서버(GameServer, ChatServer)와 명확히 구분
4. **유지보수성**: 새로운 개발자도 쉽게 이해 가능

---

## 메서드 네이밍

### Lifecycle Methods (생명주기)

| 기존 (Before) | 새 이름 (After) | 설명 |
|--------------|----------------|------|
| `Initialize()` | `InitializeMessageHandlers()` | 무엇을 초기화하는지 명시 |
| `Initialize()` | `InitializeConnectionPool()` | 연결 풀 초기화 명시 |
| `Shutdown()` | `ShutdownDatabase()` | 무엇을 종료하는지 명시 |
| `IsReady()` | `IsDatabaseReady()` | 무엇이 준비되었는지 명시 |

### Event Handlers (이벤트 핸들러)

**패턴**: `On` + `이벤트` + `동사과거형`

| 기존 (Before) | 새 이름 (After) | 설명 |
|--------------|----------------|------|
| `HandlePing()` | `OnPingMessageReceived()` | Ping 메시지 수신 시 호출 |
| `HandlePong()` | `OnPongMessageReceived()` | Pong 메시지 수신 시 호출 |
| `HandleCustomMessage()` | `OnCustomMessageReceived()` | 커스텀 메시지 수신 시 호출 |

**왜 `On` 접두사를 사용하는가?**
- 이벤트 핸들러임을 명확히 표시
- C#, JavaScript 등 다른 언어의 관례와 일치
- "무언가가 발생했을 때"라는 의미 명확

### Database Operations (데이터베이스 작업)

**패턴**: `동사` + `대상` + `세부사항`

| 기존 (Before) | 새 이름 (After) | 설명 |
|--------------|----------------|------|
| `SaveUserLogin()` | `SaveUserLoginEvent()` | 로그인 이벤트 저장 |
| `LoadUserData()` | `LoadUserProfileData()` | 유저 프로필 데이터 로드 |
| `SaveGameState()` | `PersistPlayerGameState()` | 플레이어 게임 상태 영구 저장 |
| `ExecuteQuery()` | `ExecuteCustomSqlQuery()` | 커스텀 SQL 쿼리 실행 |

**동사 선택 가이드**:
- `Save` vs `Persist`: 일반 저장 vs 영구 저장
- `Load` vs `Retrieve` vs `Fetch`: 데이터 가져오기
- `Create` vs `Generate`: 새로 만들기
- `Update` vs `Modify`: 수정하기
- `Delete` vs `Remove`: 삭제하기

---

## 변수 네이밍

### 멤버 변수 (Member Variables)

**패턴**: `m` + `형용사` + `명사`

| 기존 (Before) | 새 이름 (After) | 설명 |
|--------------|----------------|------|
| `mConnectionPool` | `mDatabaseConnectionPool` | DB 연결 풀임을 명시 |
| `mInitialized` | `mIsInitialized` | 불린 변수는 `Is`, `Has`, `Can` 접두사 |

### 로컬 변수

**명확하고 의미있는 이름 사용**

```cpp
// Bad
auto t = GetCurrentTimestamp();
auto lat = t - message.timestamp;

// Good
auto currentTimestamp = GetCurrentTimestamp();
auto roundTripLatencyMs = currentTimestamp - message.timestamp;
```

### 매개변수 (Parameters)

**패턴**: 용도를 명확히 나타내는 이름

| 기존 (Before) | 새 이름 (After) | 설명 |
|--------------|----------------|------|
| `connectionString` | `odbcConnectionString` | ODBC 연결 문자열임을 명시 |
| `maxPoolSize` | `maxConnectionPoolSize` | 연결 풀 최대 크기 명시 |
| `stateData` | `gameStateData` | 게임 상태 데이터임을 명시 |
| `query` | `sqlQuery` | SQL 쿼리임을 명시 |

### Out Parameters (출력 매개변수)

**패턴**: `out` + `변수명`

```cpp
// Clear indication this is an output parameter
bool LoadUserProfileData(uint64_t userId, std::string& outUsername);
```

---

## 네임스페이스 사용

### 구조

```cpp
namespace TestServer {
    class TestServerMessageHandler { ... }
    class TestServerDatabaseManager { ... }
}
```

**왜 클래스 이름에도 `TestServer`를 포함하는가?**

```cpp
// Without prefix - unclear
using TestServer::MessageHandler;  // Which server's message handler?

// With prefix - clear
using TestServer::TestServerMessageHandler;  // Obviously TestServer's implementation
```

---

## 상수 네이밍

### 상수

**패턴**: `k` + `PascalCase`

```cpp
constexpr int kDefaultMaxConnectionPoolSize = 10;
constexpr int kDefaultMinConnectionPoolSize = 2;
constexpr uint32_t kCustomMessageTypeStart = 1000;
```

---

## 로그 메시지 네이밍

### 패턴: `[클래스명] 동작 설명`

```cpp
// Bad - 짧고 불명확
std::cout << "[Handler] Init" << std::endl;

// Good - 명확하고 구체적
std::cout << "[TestServerMessageHandler] Message handlers initialized successfully" << std::endl;
```

### 로그 레벨별 표현

```cpp
// Info: 중요한 상태 변화
std::cout << "[TestServerDatabaseManager] Connection pool initialized with 10 connections" << std::endl;

// Error: 문제 발생
std::cerr << "[TestServerDatabaseManager] Failed to initialize connection pool: " << error << std::endl;

// Debug: 상세 정보 (변수 이름 포함)
std::cout << "[TestServerMessageHandler] PING received (connectionId: " << connectionId << ")" << std::endl;
```

---

## 주석 스타일

### 메서드 문서화

```cpp
/**
 * Persist player's game state to database
 *
 * @param userId Unique user identifier
 * @param gameStateData Serialized game state (JSON, binary, etc.)
 * @return true if game state saved successfully, false otherwise
 *
 * @note This method executes UPDATE query and requires active database connection
 * @see LoadPlayerGameState() for retrieving saved state
 */
bool PersistPlayerGameState(uint64_t userId, const std::string& gameStateData);
```

### 주석 태그

- `@param`: 매개변수 설명
- `@return`: 반환값 설명
- `@note`: 중요 참고사항
- `@see`: 관련 메서드
- `@warning`: 주의사항
- `@deprecated`: 사용 중단된 메서드

---

## 전/후 비교 예시

### MessageHandler

```cpp
// Before
class TestMessageHandler {
    void Initialize();
    void HandlePing(const Message& msg);
};

handler.Initialize();

// After
class TestServerMessageHandler {
    void InitializeMessageHandlers();
    void OnPingMessageReceived(const Message& msg);
};

messageHandler.InitializeMessageHandlers();
```

### DatabaseManager

```cpp
// Before
class TestDatabaseManager {
    bool Initialize(const std::string& connectionString, int maxPoolSize);
    bool SaveUserLogin(uint64_t userId, const std::string& username);
private:
    std::unique_ptr<ConnectionPool> mConnectionPool;
    bool mInitialized;
};

// After
class TestServerDatabaseManager {
    bool InitializeConnectionPool(const std::string& odbcConnectionString, int maxConnectionPoolSize);
    bool SaveUserLoginEvent(uint64_t userId, const std::string& username);
private:
    std::unique_ptr<ConnectionPool> mDatabaseConnectionPool;
    bool mIsInitialized;
};
```

---

## 네이밍 체크리스트

코드 작성 시 다음을 확인하세요:

- [ ] 클래스 이름이 `TestServer` 접두사를 포함하는가?
- [ ] 메서드 이름이 무엇을 하는지 명확한가?
- [ ] 이벤트 핸들러가 `On...Received` 패턴을 따르는가?
- [ ] 불린 변수가 `Is`, `Has`, `Can` 접두사를 사용하는가?
- [ ] 매개변수 이름이 용도를 명확히 나타내는가?
- [ ] 출력 매개변수가 `out` 접두사를 사용하는가?
- [ ] 로그 메시지가 클래스명과 구체적인 동작을 포함하는가?
- [ ] 주석이 "무엇을"과 "왜"를 설명하는가?

---

## 참고 자료

- [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/)
- [Microsoft C++ Coding Conventions](https://docs.microsoft.com/en-us/windows/win32/stg/coding-style-conventions)

---

**작성일**: 2024-01-XX
**버전**: 1.0
**적용 범위**: TestServer 프로젝트
