#ifndef BRIDGE_CONFIG_H
#define BRIDGE_CONFIG_H

/* Buffer sizes must stay power-of-two because ring indexing uses bit masks. */
/* 512 was too small for QConn's ~2056-byte write chunks (tx_size in eflash_loader_cfg.ini): the OUT endpoint is always re-armed regardless of ring space, so oversized bursts silently overflowed it. */
#define USB_TO_UART_RING_SIZE  8192u
#define UART_TO_USB_RING_SIZE  8192u
#define USB_TX_BUFFER_SIZE     256u

#endif /* BRIDGE_CONFIG_H */
