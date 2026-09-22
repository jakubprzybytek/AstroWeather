#ifndef INC_CONSOLE_CONSOLESERVICEBRIDGE_H_
#define INC_CONSOLE_CONSOLESERVICEBRIDGE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ConsoleService_OnUsbRxData(const uint8_t* data, uint32_t len);
/* dataTerminalReady: DTR from the host's SET_CONTROL_LINE_STATE request. */
void ConsoleService_OnHostLineState(uint8_t dataTerminalReady);
/* The host's SET_LINE_CODING request. */
void ConsoleService_OnHostLineCoding(void);

#ifdef __cplusplus
}
#endif

#endif
