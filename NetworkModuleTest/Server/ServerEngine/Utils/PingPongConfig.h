#pragma once

// PingPong 로깅 설정.
//
// 목적: 네트워크 왕복 지연(RTT) 검증 테스트에서 로그 출력 빈도를 제어한다.
//   - 기본(비활성): PINGPONG_LOG_INTERVAL 패킷마다 1회 출력 → 고빈도 테스트에서
//     콘솔/파일 I/O 오버헤드를 줄인다.
//   - verbose 모드: 모든 ping/pong마다 로그 출력 → 패킷 손실·순서 역전 디버깅에 사용.
//
// verbose 모드 활성화 방법:
//   아래 주석을 해제하거나 프로젝트 전처리기 정의에 ENABLE_PINGPONG_VERBOSE_LOG를 추가.

// #define ENABLE_PINGPONG_VERBOSE_LOG

// N번째 ping/pong마다 로그를 출력한다 (기본값: 10).
// 값을 1로 설정하면 verbose 모드와 동일한 효과.
#ifndef PINGPONG_LOG_INTERVAL
#define PINGPONG_LOG_INTERVAL 10
#endif
