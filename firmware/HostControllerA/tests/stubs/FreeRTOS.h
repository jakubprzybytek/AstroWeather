#pragma once

// Host-side stand-in for the FreeRTOS kernel header: static control-block
// types sized generously, and the heap mapped onto malloc/free.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t TickType_t;
typedef long BaseType_t;
typedef unsigned long UBaseType_t;

typedef struct { uint8_t opaque[128]; } StaticTask_t;
typedef struct { uint8_t opaque[96]; } StaticQueue_t;
typedef StaticQueue_t StaticSemaphore_t;

#define pdTRUE  ((BaseType_t)1)
#define pdFALSE ((BaseType_t)0)
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

void* pvPortMalloc(size_t size);
void vPortFree(void* pointer);
size_t xPortGetFreeHeapSize(void);
size_t xPortGetMinimumEverFreeHeapSize(void);

#ifdef __cplusplus
}
#endif
