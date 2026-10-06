#include <Utils/MicroClock.hpp>

#include "main.h"
#include "FreeRTOS.h"
#include "task.h"

namespace Utils {

uint32_t microsNow()
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint32_t ticks = xTaskGetTickCountFromISR();
    uint32_t value = SysTick->VAL;
    if ((SCB->ICSR & SCB_ICSR_PENDSTSET_Msk) != 0U) {
        // SysTick wrapped and its interrupt has not run: count that tick, and
        // read the counter again in case it wrapped after the first read.
        value = SysTick->VAL;
        ++ticks;
    }
    __set_PRIMASK(primask);

    const uint32_t reload = SysTick->LOAD + 1U;
    const uint32_t intoTick = (reload - 1U - value) * 1000U / reload;
    return ticks * 1000U + intoTick;
}

} // namespace Utils
