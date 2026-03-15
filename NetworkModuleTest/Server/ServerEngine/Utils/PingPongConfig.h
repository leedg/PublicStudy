#pragma once

// PingPong logging configuration
//          Define ENABLE_PINGPONG_VERBOSE_LOG to enable per-ping/pong debug logs.
//          When disabled, logs are printed every PINGPONG_LOG_INTERVAL packets.
//          When enabled, every single ping/pong is logged (verbose).
//
//
// Usage:
//   To enable verbose logging, uncomment the line below or add
//   ENABLE_PINGPONG_VERBOSE_LOG to the preprocessor definitions in your project.

// #define ENABLE_PINGPONG_VERBOSE_LOG

// Log interval - print log every N-th ping/pong (default: 10)
#ifndef PINGPONG_LOG_INTERVAL
#define PINGPONG_LOG_INTERVAL 10
#endif
