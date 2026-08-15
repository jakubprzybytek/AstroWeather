#ifndef BRIDGE_CONFIG_H
#define BRIDGE_CONFIG_H

/* Buffer sizes must stay power-of-two because ring indexing uses bit masks. */
#define USB_TO_UART_RING_SIZE  512u
#define UART_TO_USB_RING_SIZE  4096u
#define USB_TX_BUFFER_SIZE     256u

#endif /* BRIDGE_CONFIG_H */
