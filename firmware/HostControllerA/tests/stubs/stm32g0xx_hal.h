#pragma once

// Host-side stand-in for the STM32G0 HAL, enough for Core/Inc/main.h and the
// User code under test to compile natively. Behaviour lives in StubHal.cpp and
// is steered from tests through StubHal.hpp. Add declarations as code under
// test needs them; keep names and values matching the real HAL.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_OK = 0x00U,
    HAL_ERROR = 0x01U,
    HAL_BUSY = 0x02U,
    HAL_TIMEOUT = 0x03U
} HAL_StatusTypeDef;

typedef enum {
    EXTI4_15_IRQn = 7
} IRQn_Type;

/* GPIO -------------------------------------------------------------------- */

typedef struct {
    uint32_t index; /* 0 = A, 1 = B, ... */
} GPIO_TypeDef;

typedef struct {
    uint32_t Pin;
    uint32_t Mode;
    uint32_t Pull;
    uint32_t Speed;
    uint32_t Alternate;
} GPIO_InitTypeDef;

typedef enum {
    GPIO_PIN_RESET = 0U,
    GPIO_PIN_SET
} GPIO_PinState;

#define GPIO_PIN_0  ((uint16_t)0x0001)
#define GPIO_PIN_1  ((uint16_t)0x0002)
#define GPIO_PIN_2  ((uint16_t)0x0004)
#define GPIO_PIN_3  ((uint16_t)0x0008)
#define GPIO_PIN_4  ((uint16_t)0x0010)
#define GPIO_PIN_5  ((uint16_t)0x0020)
#define GPIO_PIN_6  ((uint16_t)0x0040)
#define GPIO_PIN_7  ((uint16_t)0x0080)
#define GPIO_PIN_8  ((uint16_t)0x0100)
#define GPIO_PIN_9  ((uint16_t)0x0200)
#define GPIO_PIN_10 ((uint16_t)0x0400)
#define GPIO_PIN_11 ((uint16_t)0x0800)
#define GPIO_PIN_12 ((uint16_t)0x1000)
#define GPIO_PIN_13 ((uint16_t)0x2000)
#define GPIO_PIN_14 ((uint16_t)0x4000)
#define GPIO_PIN_15 ((uint16_t)0x8000)

#define GPIO_MODE_INPUT     0x00000000U
#define GPIO_MODE_OUTPUT_PP 0x00000001U
#define GPIO_MODE_ANALOG    0x00000003U

#define GPIO_NOPULL   0x00000000U
#define GPIO_PULLUP   0x00000001U
#define GPIO_PULLDOWN 0x00000002U

extern GPIO_TypeDef stubGpioPorts[4];
#define GPIOA (&stubGpioPorts[0])
#define GPIOB (&stubGpioPorts[1])
#define GPIOC (&stubGpioPorts[2])
#define GPIOD (&stubGpioPorts[3])

void HAL_GPIO_Init(GPIO_TypeDef* port, GPIO_InitTypeDef* init);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef* port, uint16_t pin);
void HAL_GPIO_WritePin(GPIO_TypeDef* port, uint16_t pin, GPIO_PinState state);

/* Peripheral handles (opaque here) ---------------------------------------- */

typedef struct { uint32_t unused; } TIM_HandleTypeDef;
typedef struct { uint32_t unused; } I2C_HandleTypeDef;
typedef struct { uint32_t unused; } SPI_HandleTypeDef;
typedef struct { uint32_t unused; } ADC_HandleTypeDef;
typedef struct { uint32_t unused; } RTC_HandleTypeDef;

#define RTC_BKP_DR0 0x00000000U
#define RTC_BKP_DR1 0x00000001U

/* Time --------------------------------------------------------------------- */

uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t delayMs);

#ifdef __cplusplus
}
#endif
