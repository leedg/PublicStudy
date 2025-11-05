# RIOEchoServer (VS2022)

- **Accept path:** IOCP + AcceptEx (전용 스레드)
- **Data path:** RIO (Registered I/O)
- **튜닝:** Per-core CQ/RQ 샤딩, CQ 폴링(초저지연), 고정 슬라이스 등록 버퍼 풀, 세션별 사전 RECV 포스트
- **개발환경:** Visual Studio 2022 (MSVC), Windows 10/11, Windows 8+ (RIO)

## 빌드
1. `RIOEchoServer.sln` 더블클릭 → VS2022 열기
2. x64/Release로 빌드 권장

## 실행
```
RIOEchoServer.exe [port]
```
기본 포트: 5050

## 구조
```
RIOEchoServer/
├─ RIOEchoServer.sln
└─ RIOEchoServer/
   ├─ RIOEchoServer.vcxproj
   ├─ main.cpp
   ├─ RIONetwork.h / RIONetwork.cpp   (RIO 함수테이블/전역 관리, CQ 생성, 설정)
   ├─ BufferPool.h  / BufferPool.cpp  (등록 버퍼 풀: 고정 1KB 슬라이스)
   ├─ Session.h     / Session.cpp     (세션 단위 RQ 생성/Send/Recv/완료 처리)
   ├─ RIOWorker.h   / RIOWorker.cpp   (코어별 CQ 폴링 워커, 세션 할당/운영)
   ├─ IOCPAcceptor.h/ IOCPAcceptor.cpp(accept 스레드, 라운드로빈 워커 배치)
   └─ Config.h                         (튜닝 파라미터)
```

## 주요 튜닝 포인트
- **Per-core CQ/RQ**: CPU 개수만큼 워커(CQ) 생성, 세션을 라운드로빈으로 분배
- **폴링 모드**: RIO CQ는 이벤트를 생성하되 기본은 `RIODequeueCompletion`을 폴링
- **버퍼 등록**: 64MB 등록 버퍼 → 1KB 슬라이스로 분할. 슬라이스 재사용
- **Pre-post Recv**: 세션당 RECV를 충분히 포스트(기본 128개)
- **스핀 대기**: `YieldProcessor()`로 가벼운 대기 (필요시 sleep 주기 조정)

> 프로덕션에서는: 세션 종료 정교화, 백프레셔, 오류 리커버리, 통계/모니터링, TLS 등을 추가하세요.
