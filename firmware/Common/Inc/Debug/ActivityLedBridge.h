#ifndef INC_DEBUG_ACTIVITYLEDBRIDGE_H_
#define INC_DEBUG_ACTIVITYLEDBRIDGE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pulses LED2 for `ms`, from a task or an interrupt; see Debug/PulseLed.hpp. */
void ActivityLed_Pulse(uint32_t ms);

/* The pulse for one transfer on the host's USB CDC, I2C or ST67 SPI link. */
#define ACTIVITY_LED_COMMS_PULSE_MS 20U

#ifdef __cplusplus
}
#endif

#endif
