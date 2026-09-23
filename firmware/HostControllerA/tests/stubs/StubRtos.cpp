#include "StubHal.hpp"

#include "FreeRTOS.h"
#include "cmsis_os2.h"

#include <cstdlib>

// Nothing is scheduled natively: threads are never created, mutexes always
// succeed, delays advance the fake clock, and thread flags are not delivered.

extern "C" {

osThreadId_t osThreadNew(osThreadFunc_t, void*, const osThreadAttr_t*)
{
    return nullptr;
}

uint32_t osThreadFlagsSet(osThreadId_t, uint32_t flags)
{
    return flags;
}

uint32_t osThreadFlagsWait(uint32_t, uint32_t, uint32_t timeout)
{
    if (timeout != osWaitForever) {
        Stub::advanceTick(timeout);
    }
    return osFlagsErrorTimeout;
}

osMutexId_t osMutexNew(const osMutexAttr_t* attr)
{
    // Any non-null handle will do; the control block is a convenient one.
    return attr != nullptr && attr->cb_mem != nullptr ? attr->cb_mem
                                                      : reinterpret_cast<void*>(1);
}

osStatus_t osMutexAcquire(osMutexId_t, uint32_t)
{
    return osOK;
}

osStatus_t osMutexRelease(osMutexId_t)
{
    return osOK;
}

osStatus_t osDelay(uint32_t ticks)
{
    Stub::advanceTick(ticks);
    return osOK;
}

uint32_t osKernelGetTickCount(void)
{
    return Stub::tick();
}

void* pvPortMalloc(size_t size)
{
    return std::malloc(size);
}

void vPortFree(void* pointer)
{
    std::free(pointer);
}

size_t xPortGetFreeHeapSize(void)
{
    return 0U;
}

size_t xPortGetMinimumEverFreeHeapSize(void)
{
    return 0U;
}

} // extern "C"
