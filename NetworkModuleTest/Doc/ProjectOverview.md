# NetworkModuleTest 프로젝트 개요

## 🌐 Overview (English & 한국어)

This project implements a high-performance asynchronous network module for distributed server architecture with cross-platform support.

이 프로젝트는 크로스플랫폼을 지원하는 고성능 비동기 네트워크 모듈로 분산 서버 아키텍처를 구현합니다.

## 📁 프로젝트 구조

```
NetworkModuleTest/
├── 📚 Doc/                           # 📖 프로젝트 문서
├── 🖥️ Server/                        # 🏗️ 서버 애플리케이션
│   ├── ServerEngine/                 # ⚙️ 네트워크/DB/스트림 유틸리티 엔진
│   ├── TestServer/                   # 🧪 로직 처리 서버
│   └── DBServer/                     # 🗄️ 데이터베이스 처리 서버
├── 📡 Client/                        # 💬 클라이언트 통신 모듈
│   └── Network/                      # 🔌 네트워크 통신 전용
├── 🏛️ Legacy/                       # 📜 기존 코드 보관
│   └── NetworkModuleTest/            # 📋 원본 멀티플랫폼 네트워크
└── 📋 Tools/                         # 🔧 빌드/테스트 도구
```

## 🎯 모듈별 역할

### 1. MultiPlatformNetwork (멀티플랫폼 네트워크)
- **목적**: 크로스플랫폼 비동기 네트워크 지원
- **플랫폼**: Windows(IOCP/RIO), Linux(epoll/io_uring), macOS(kqueue)
- **상태**: ✅ 완료 (기존 코드 보관)

### 2. ServerEngine (서버 엔진)
- **목적**: 네트워크, DB, 스트림, 시간, 로깅 등 유틸리티 통합
- **구성요소**:
  - **Core**: 핵심 네트워크 추상화 레이어
  - **Protocols**: Protobuf 기반 통신 프로토콜
  - **Utils**: 시간, 버퍼, 스레드, 로깅 유틸리티
- **상태**: 🔄 구현 중

### 3. TestServer (로직 서버)
- **목적**: 클라이언트 요청 처리 및 비즈니스 로직 수행
- **기능**:
  - 클라이언트 연결 관리
  - 요청 인증 및 권한 확인
  - 비즈니스 로직 처리
  - DBServer로 데이터 요청 전송
- **상태**: ⏳ 대기 중

### 4. DBServer (데이터베이스 서버)
- **목적**: 데이터베이스 CRUD 작업 전담
- **기능**:
  - 데이터 조회/삽입/수정/삭제
  - 트랜잭션 관리
  - 커넥션 풀 관리
- **상태**: ⏳ 대기 중

### 5. Client/Network (클라이언트 통신)
- **목적**: 통신 기능에 특화된 클라이언트 모듈
- **기능**:
  - 서버 연결 관리
  - 메시지 송수신
  - 자동 재연결
- **상태**: ⏳ 대기 중

## 🏗️ 아키텍처 흐름

```
클라이언트 ←→ TestServer ←→ DBServer ←→ 데이터베이스
   ↑           ↑            ↑
   │           │            │
Client/    ServerEngine    ServerEngine
 Network   (네트워크)    (데이터베이스)
```

## 📋 개발 현황 (Development Status)

| 모듈 | 상태 | 진행률 | 비고 |
|------|------|--------|------|
| MultiPlatformNetwork | ✅ 완료 | 100% | 기존 코드 보관 (Reference Archive) |
| ServerEngine | 🔄 구현 중 | 60% | 코어, 유틸리티, 프로토콜 |
| TestServer | ⏳ 대기 중 | 0% | ServerEngine 의존 |
| DBServer | ⏳ 대기 중 | 0% | ServerEngine 의존 |
| Client/Network | ⏳ 대기 중 | 0% | ServerEngine 의존 |
| 문서 | ✅ 완료 | 95% | 포괄적 문서화 완료 |

### 최신 업데이트 (Latest Updates)
- ✅ ServerEngine.vcxproj 프로젝트 파일 완성
- ✅ 모든 소스 파일 및 필터 구조 추가
- ✅ PingPong.h 헤더 파일 복원
- ✅ MultiPlatformNetwork 프로젝트 추가
- ✅ README.md 문서화 및 동기화 완료

## 🚀 다음 단계

1. **ServerEngine 완성**
   - 핵심 네트워크 엔진 구현
   - 유틸리티 라이브러리 구현
   - Protobuf 통합

2. **TestServer 구현**
   - ServerEngine 연동
   - 클라이언트 처리 로직
   - DBServer 통신 준비

3. **DBServer 구현**
   - ServerEngine 연동
   - 데이터베이스 연동
   - 트랜잭션 처리

4. **Client/Network 구현**
   - 간단한 통신 인터페이스
   - 자동 재연결 기능
   - 메시지 큐 관리

## 🔧 기술 스택

- **언어**: C++17
- **빌드**: CMake
- **네트워크**: AsyncIO (IOCP/epoll/kqueue)
- **직렬화**: Protobuf
- **데이터베이스**: TBD (MySQL/PostgreSQL/SQLite)
- **테스트**: GTest

---

## 🔗 관련 문서 (Related Documentation)

- [README.md](../README.md) - Main project documentation (English)
- [Architecture.md](./Architecture.md) - Detailed architecture specification
- [DevelopmentGuide.md](./DevelopmentGuide.md) - Comprehensive development guide
- [MultiPlatformNetwork/Doc/](../ModuleTest/MultiPlatformNetwork/Doc/) - Technical network documentation

---

*이 문서는 프로젝트 진행에 따라 지속적으로 업데이트됩니다.*  
*This document is continuously updated as the project progresses.*