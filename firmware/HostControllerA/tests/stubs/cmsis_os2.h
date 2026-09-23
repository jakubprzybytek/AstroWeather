#pragma once

// Host-side stand-in for CMSIS-RTOS2. Nothing is scheduled: threads are never
// started, mutexes are no-ops, and the kernel tick is the fake clock from
// StubHal.hpp. Implementations are in StubRtos.cpp.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* osThreadId_t;
typedef void* osMutexId_t;
typedef void* osMessageQueueId_t;
typedef void* osSemaphoreId_t;
typedef void (*osThreadFunc_t)(void* argument);

typedef enum {
    osOK = 0,
    osError = -1,
    osErrorTimeout = -2,
    osErrorResource = -3,
    osErrorParameter = -4
} osStatus_t;

typedef enum {
    osPriorityNone = 0,
    osPriorityIdle = 1,
    osPriorityLow = 8,
    osPriorityBelowNormal = 16,
    osPriorityNormal = 24,
    osPriorityAboveNormal = 32,
    osPriorityHigh = 40,
    osPriorityRealtime = 48,
    osPriorityISR = 56
} osPriority_t;

#define osWaitForever 0xFFFFFFFFU
#define osFlagsWaitAny 0x00000000U
#define osFlagsWaitAll 0x00000001U
#define osFlagsNoClear 0x00000002U
#define osFlagsError 0x80000000U
#define osFlagsErrorTimeout 0xFFFFFFFEU
#define osMutexPrioInherit 0x00000002U

typedef struct {
    const char* name;
    uint32_t attr_bits;
    void* cb_mem;
    uint32_t cb_size;
    void* stack_mem;
    uint32_t stack_size;
    osPriority_t priority;
    uint32_t tz_module;
    uint32_t reserved;
} osThreadAttr_t;

typedef struct {
    const char* name;
    uint32_t attr_bits;
    void* cb_mem;
    uint32_t cb_size;
} osMutexAttr_t;

typedef struct {
    const char* name;
    uint32_t attr_bits;
    void* cb_mem;
    uint32_t cb_size;
    void* mq_mem;
    uint32_t mq_size;
} osMessageQueueAttr_t;

osThreadId_t osThreadNew(osThreadFunc_t func, void* argument, const osThreadAttr_t* attr);
uint32_t osThreadFlagsSet(osThreadId_t thread, uint32_t flags);
uint32_t osThreadFlagsWait(uint32_t flags, uint32_t options, uint32_t timeout);

osMutexId_t osMutexNew(const osMutexAttr_t* attr);
osStatus_t osMutexAcquire(osMutexId_t mutex, uint32_t timeout);
osStatus_t osMutexRelease(osMutexId_t mutex);

osStatus_t osDelay(uint32_t ticks);
uint32_t osKernelGetTickCount(void);

#ifdef __cplusplus
}
#endif
