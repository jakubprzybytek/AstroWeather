#ifndef INC_CONSOLE_CONSOLESERVICEBRIDGE_H_
#define INC_CONSOLE_CONSOLESERVICEBRIDGE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ConsoleService_OnUsbRxData(const uint8_t* data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif
