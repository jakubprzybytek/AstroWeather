#pragma once

#include "main.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

HAL_StatusTypeDef SCT2xxx_Send(uint8_t value);
void SCT2xxx_Enable(void);
void SCT2xxx_Disable(void);

#ifdef __cplusplus
}
#endif