#ifndef INC_DEBUG_ACTIVITYLEDBRIDGE_H_
#define INC_DEBUG_ACTIVITYLEDBRIDGE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pulses LED2 for `ms`, from a task or an interrupt; see Debug/PulseLed.hpp. */
void ActivityLed_Pulse(uint32_t ms);

/* The pulse for one USB CDC transfer, in either direction. */
#define ACTIVITY_LED_USB_PULSE_MS 20U

#ifdef __cplusplus
}
#endif

#endif
