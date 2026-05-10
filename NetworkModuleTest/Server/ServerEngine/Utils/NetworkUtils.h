#pragma once

// NetworkModule 통합 유틸리티 헤더.
// 모든 유틸리티를 한 번에 포함할 때 사용한다.
//
// 필요한 유틸리티만 선택적으로 포함하려면 개별 헤더를 직접 include한다:
//   - NetworkTypes.h  : 타입 별칭 및 네트워크 상수
//   - Timer.h         : 경과 시간 측정 (steady_clock 기반)
//   - StringUtils.h   : std::string 조작 유틸리티
//   - StringUtil.h    : 고정 크기 char[] 버퍼 안전 연산 (copy/format/append)
//   - SafeQueue.h     : 스레드 안전 큐
//   - ThreadPool.h    : 비동기 태스크용 스레드 풀
//   - Logger.h        : 레벨 필터링 + 파일/콘솔 로거

#include "NetworkTypes.h"
#include "Timer.h"
#include "StringUtils.h"
#include "StringUtil.h"
#include "SafeQueue.h"
#include "ThreadPool.h"
#include "Logger.h"
