#include "TimeWheel.h"

void TimeWheel::Update()
{
	uint64_t now = GetTickCount64();

	while (now - _lastTick >= WHEEL_TICK)
	{
		_cursor = (_cursor + 1) % WHEEL_SIZE;
		_lastTick += WHEEL_TICK;

		auto& jobs = _slots[_cursor];
		for (auto& job : jobs)
		{
			if (job.callback) job.callback(); 
		}
		jobs.clear();
	}
}
