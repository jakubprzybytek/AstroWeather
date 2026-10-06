#pragma once

#include <cstdint>

namespace Utils {

// Microseconds of this board's own clock, from the FreeRTOS tick (1 kHz) and
// SysTick's count within the tick. Wraps after about 71 minutes, so compare
// times by unsigned difference. Safe from any interrupt and from a task:
// interrupts are off for the few reads, and a tick that SysTick has counted
// but its interrupt has not yet handled is added in. While the scheduler is
// suspended the kernel holds ticks back, so a time read then can be a
// millisecond or so early; the sync tolerates that.
uint32_t microsNow();

} // namespace Utils
