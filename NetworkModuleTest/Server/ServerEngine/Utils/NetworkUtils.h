#pragma once

// Unified utility functions for NetworkModule
//
// This header includes all utility classes for convenience
//
// Individual headers can be included separately if needed:
//   - NetworkTypes.h    : Type definitions and constants
//   - Timer.h           : Time measurement utility
//   - StringUtils.h     : std::string manipulation utilities
//   - StringUtil.h      : Safe char[] buffer operations (copy/format/append)
//   - SafeQueue.h       : Thread-safe queue
//   - ThreadPool.h      : Thread pool for async tasks
//   - Logger.h          : Logging utility

#include "NetworkTypes.h"
#include "Timer.h"
#include "StringUtils.h"
#include "StringUtil.h"
#include "SafeQueue.h"
#include "ThreadPool.h"
#include "Logger.h"
