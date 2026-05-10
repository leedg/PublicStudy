#pragma once

// 경과 시간 측정을 위한 스톱워치 스타일 타이머.

#include "NetworkTypes.h"
#include <chrono>

namespace Network::Utils
{
// =============================================================================
// Timer
//
// 생성(또는 Reset()) 시점부터 경과한 시간을 밀리초 단위로 반환한다.
// =============================================================================

class Timer
{
public:
	// 타이머를 현재 시각으로 초기화하고 시작한다.
	Timer() : mStartTime(GetCurrentTimestamp()) {}

	// 타이머 생성/리셋 이후 경과 시간을 밀리초 단위로 반환한다.
	Timestamp GetElapsedTime() const
	{
		return GetCurrentTimestamp() - mStartTime;
	}

	// 타이머의 기준 시각을 현재 시각으로 갱신한다.
	void Reset() { mStartTime = GetCurrentTimestamp(); }

	// 현재 타임스탬프를 밀리초 단위로 반환한다.
	// steady_clock을 사용하므로 NTP 보정으로 인한 시스템 시간 역행에 영향받지 않는다.
	static Timestamp GetCurrentTimestamp()
	{
		auto now = std::chrono::steady_clock::now();
		auto duration = now.time_since_epoch();
		return std::chrono::duration_cast<std::chrono::milliseconds>(duration)
			.count();
	}

private:
	Timestamp mStartTime;  // 마지막 Reset() 또는 생성 시점의 steady_clock 밀리초 값
};

} // namespace Network::Utils
