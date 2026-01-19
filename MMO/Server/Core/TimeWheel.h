#pragma once

#define WHEEL_TICK 100
#define WHEEL_SIZE 50
#include <functional>

#include "IocpCore.h"

class TimeWheel
{
	struct TimerJob { std::function<void()> callback; };
	std::vector<TimerJob> _slots[WHEEL_SIZE];
	int _cursor = 0;
	uint64_t _lastTick = GetTickCount64();

public:
	void AddTimer(int delayMs, std::function<void()> callback)
	{
		int ticks = delayMs / WHEEL_TICK;
		int targetSlot = (_cursor + ticks) % WHEEL_SIZE;
		_slots[targetSlot].push_back({ callback });
	}

	void Update();
};

