# 개발 가이드

## 🚀 빌드 및 실행

### 전제 조건
- C++17 컴파일러
- CMake 3.15+
- Protobuf (libprotobuf-dev, protobuf-compiler)
- GTest (선택사항)

### 빌드 절차

#### 1. NetworkModuleTest 빌드
```bash
# 네트워크 엔진 빌드
cd NetworkModuleTest/Server/ServerEngine
mkdir build && cd build
cmake ..
make -j$(nproc)

# 테스트 실행
./tests/NetworkModuleTests
```

#### 2. TestServer 빌드
```bash
cd NetworkModuleTest/Server/TestServer
mkdir build && cd build
cmake ..
make -j$(nproc)

# 서버 실행
./TestServer --port 8001
```

#### 3. DBServer 빌드
```bash
cd NetworkModuleTest/Server/DBServer
mkdir build && cd build
cmake ..
make -j$(nproc)

# 서버 실행
./DBServer --port 8002 --db-host localhost --db-name networkdb
```

## 🏗️ 모듈별 빌드 순서

1. **ServerEngine** (필수)
   - Core 네트워크 엔진
   - 유틸리티 라이브러리
   - Protobuf 코드 생성

2. **DBServer** (ServerEngine 의존)
   - 데이터베이스 연결
   - CRUD API 구현

3. **TestServer** (ServerEngine, DBServer 의존)
   - 클라이언트 연결 관리
   - 비즈니스 로직 처리

4. **Client/Network** (ServerEngine 의존)
   - 통신 전용 클라이언트

## 🧪 테스트 방법

### 단위 테스트
```bash
# 각 모듈별 테스트
cd NetworkModuleTest/Server/ServerEngine/build
ctest -V
```

### 통합 테스트
```bash
# DBServer 시작
./DBServer --port 8002 &

# TestServer 시작
./TestServer --port 8001 --db-server localhost:8002 &

# 클라이언트 테스트
cd NetworkModuleTest/Client/Network
./ClientTest --server localhost:8001
```

## 📝 코드 컨벤션

### 네이밍 규칙
- **클래스**: PascalCase (NetworkEngine, MessageHandler)
- **함수**: PascalCase (SendMessage, ProcessData)
- **변수**: camelCase (connectionId, bufferSize)
- **상수**: UPPER_SNAKE_CASE (MAX_CONNECTIONS, DEFAULT_PORT)

### 파일 구조
- **헤더**: `.h` 파일은 `include/` 디렉토리
- **소스**: `.cpp` 파일은 `src/` 디렉토리
- **테스트**: `*_test.cpp` 형식

### 주석 규칙
```cpp
/**
 * English: Brief description
 * 한글: 간단한 설명
 * @param paramName Parameter description
 * @return Return value description
 */
```

## 🔧 설정 관리

### 환경 변수
```bash
export NETWORK_LOG_LEVEL=INFO
export NETWORK_MAX_CONNECTIONS=10000
export NETWORK_TIMEOUT_MS=30000
export DATABASE_HOST=localhost
export DATABASE_PORT=5432
```

### 설정 파일
```json
{
  "server": {
    "port": 8001,
    "maxConnections": 10000,
    "timeout": 30000
  },
  "database": {
    "host": "localhost",
    "port": 5432,
    "name": "networkdb",
    "user": "postgres",
    "password": "password"
  }
}
```

## 🐛 디버깅

### 로그 레벨
- **DEBUG**: 상세 디버깅 정보
- **INFO**: 일반 정보
- **WARN**: 경고
- **ERROR**: 에러

### 디버깅 옵션
```bash
# Debug 빌드
cmake -DCMAKE_BUILD_TYPE=Debug ..

# 로그 레벨 설정
./TestServer --log-level DEBUG

# 주소 검사 도구 사용
valgrind --leak-check=full ./TestServer
```

## 🚀 배포

### 빌드 아티팩트
```
build/
├── bin/
│   ├── TestServer
│   ├── DBServer
│   └── NetworkTest
├── lib/
│   ├── libServerEngine.a
│   └── libNetworkUtils.a
└── include/
    └── NetworkModule/
```

### 도커 배포
```dockerfile
FROM ubuntu:20.04
RUN apt-get update && apt-get install -y \
    build-essential cmake \
    libprotobuf-dev protobuf-compiler
COPY . /app
WORKDIR /app/build
RUN cmake .. && make -j$(nproc)
CMD ["./bin/TestServer"]
```

---

*이 문서는 개발 과정에서 지속적으로 업데이트됩니다.*