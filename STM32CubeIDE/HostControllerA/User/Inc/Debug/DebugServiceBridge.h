/*
 * DebugServiceBridge.h
 *
 * Plain-C bridge so USB_Device/App/usbd_cdc_if.c can hand received bytes
 * to the C++ DebugService without exposing C++ types to CubeMX-generated code.
 */

#ifndef INC_DEBUG_DEBUGSERVICEBRIDGE_H_
#define INC_DEBUG_DEBUGSERVICEBRIDGE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Safe to call from USB interrupt context. */
void DebugService_OnUsbRxData(const uint8_t* data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* INC_DEBUG_DEBUGSERVICEBRIDGE_H_ */
