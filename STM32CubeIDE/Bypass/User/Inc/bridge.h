#ifndef BRIDGE_H
#define BRIDGE_H

#include <stdint.h>

void Bridge_Init(void);
void Bridge_Process(void);
void Bridge_OnUsbRx(const uint8_t *data, uint32_t length);
void Bridge_OnUsbTxComplete(void);

#endif /* BRIDGE_H */
