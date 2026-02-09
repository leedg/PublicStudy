#pragma once

// English: PingPong logging configuration
//          Define ENABLE_PINGPONG_VERBOSE_LOG to enable per-ping/pong debug logs.
//          When disabled, logs are printed every PINGPONG_LOG_INTERVAL packets.
//          When enabled, every single ping/pong is logged (verbose).
//
// 한글: PingPong 로깅 설정
//       ENABLE_PINGPONG_VERBOSE_LOG를 정의하면 모든 ping/pong에 대해 디버그 로그 출력.
//       비활성화 시 PINGPONG_LOG_INTERVAL 패킷마다 1번 로그 출력.
//       활성화 시 모든 ping/pong 로그 출력 (verbose 모드).
//
// Usage:
//   To enable verbose logging, uncomment the line below or add
//   ENABLE_PINGPONG_VERBOSE_LOG to the preprocessor definitions in your project.

// #define ENABLE_PINGPONG_VERBOSE_LOG

// English: Log interval - print log every N-th ping/pong (default: 10)
// 한글: 로그 간격 - N번째 ping/pong마다 로그 출력 (기본값: 10)
#ifndef PINGPONG_LOG_INTERVAL
#define PINGPONG_LOG_INTERVAL 10
#endif
