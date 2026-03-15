#pragma once

// English: Timer utility for time measurement
// 한글: 시간 측정을 위한 타이머 유틸리티

#include "NetworkTypes.h"
#include <chrono>

namespace Network::Utils
{
// =============================================================================
// English: Timer - simple stopwatch-style timer
// 한글: Timer - 단순한 스톱워치 스타일 타이머
// =============================================================================

class Timer
{
public:
	// English: Constructor - starts the timer
	// 한글: 생성자 - 타이머 시작
	Timer() : mStartTime(GetCurrentTimestamp()) {}

	// English: Get elapsed time since timer creation/reset
	// 한글: 타이머 생성/리셋 이후 경과 시간 가져오기
	Timestamp GetElapsedTime() const
	{
		return GetCurrentTimestamp() - mStartTime;
	}

	// English: Reset the timer to current time
	// 한글: 타이머를 현재 시간으로 리셋
	void Reset() { mStartTime = GetCurrentTimestamp(); }

	// English: Get current timestamp in milliseconds
	// 한글: 현재 타임스탬프를 밀리초 단위로 가져오기
	static Timestamp GetCurrentTimestamp()
	{
		// English: Use steady_clock for elapsed time measurement to prevent issues
		// caused by NTP clock adjustments (system time going backwards).
		// 한글: steady_clock을 사용하여 NTP 시계 조정으로 인한 문제(시스템 시간 역행)를 방지합니다.
		auto now = std::chrono::steady_clock::now();
		auto duration = now.time_since_epoch();
		return std::chrono::duration_cast<std::chrono::milliseconds>(duration)
			.count();
	}

private:
	Timestamp mStartTime;
};

} // namespace Network::Utils
