# ST67 Bypass and Bootloader Project Guide

## Purpose

Extend an already bootstrapped, working STM32G0B1 CMake firmware that currently echoes data over USB CDC. Its final job is to connect a PC to the ST67:

```text
PC USB CDC <-> STM32G0B1 USB device <-> USART2 <-> ST67
```

The starting project is otherwise empty and has no access to the mission application's source code. It already builds, flashes, enumerates as a CDC device, receives USB data, and sends the echo response. Preserve that working USB setup while replacing echo behavior with a foreground superloop, interrupt-driven UART reception, and bidirectional forwarding. Do not add FreeRTOS.

The bypass transport must be binary-transparent. This is required for a bootloader protocol and is also the least surprising behavior for manufacture-mode access. Do not parse lines, echo characters, translate line endings, strip ANSI escape sequences, or interpret payload bytes in the bridge.

## Hardware and Protocol Inputs

Treat the following as the complete implementation inputs; no external application code is required.

| Signal | STM32 pin | Configuration |
|---|---:|---|
| `ST67_CHIP_EN` | PA0 | GPIO push-pull output |
| `ST67_TX` | PA2 | USART2 TX, AF1, very-high speed, no pull |
| `ST67_RX` | PA3 | USART2 RX, AF1, very-high speed, no pull |
| `ST67_RDY` | PA4 | GPIO input, no pull |
| `ST67_CS` | PA5 | GPIO push-pull output; high while using UART |
| `ST67_BOOT` | PB1 | GPIO push-pull output |
| USB DM | PA11 | USB DRD FS |
| USB DP | PA12 | USB DRD FS |

The proven UART format is:

- USART2
- 2,000,000 baud
- 8 data bits
- no parity
- 1 stop bit
- no hardware flow control
- oversampling by 16
- peripheral clock from PCLK1
- UART FIFO enabled
- RX FIFO threshold 1/8

Configure USART2 directly at 2,000,000 baud.

The working manufacture-mode pin state is:

```text
ST67_CS      = 1
ST67_BOOT    = 0
ST67_CHIP_EN = 1
```

Use a deterministic entry sequence rather than relying on reset defaults:

```text
1. Drive ST67_CHIP_EN low.
2. Drive ST67_CS high.
3. Drive ST67_BOOT low for manufacture mode.
4. Wait at least 20 ms.
5. Drive ST67_CHIP_EN high.
6. Wait for ST67 startup before expecting traffic.
```

Use a 20 ms low pulse and initially wait at least 20 ms after raising `CHIP_EN`. Increase the final startup delay if the ST67 documentation requires it. `ST67_RDY` may be observed for diagnostics, but bypass traffic does not need to be gated on it.

### Bootloader mode is not yet electrically specified

Do not infer the ST67 bootloader strap from the name `ST67_BOOT` alone. The only established mode is that `BOOT=0`, `CS=1`, and an enable/reset cycle enters the observed manufacture shell. Before implementing bootloader entry, obtain the ST67W6 hardware/programming documentation and record:

- required `BOOT` level;
- required `CS` level;
- whether any additional strap pin is sampled;
- setup time before `CHIP_EN` rises;
- minimum reset/enable pulse width;
- UART baud and framing used by the ROM bootloader;
- bootloader handshake, timeout, and reset-exit behavior.

Keep mode selection isolated in one function so those verified values do not leak into the transport code.

## Integration Scope

Keep the existing project layout and add only a small bridge module. A typical resulting layout is:

```text
CMakeLists.txt
CMakePresets.json
<project>.ioc
cmake/
Core/
  Inc/
    main.h
    bridge.h
  Src/
    main.c
    bridge.c
    stm32g0xx_it.c
    stm32g0xx_hal_msp.c
USB_Device/
Drivers/
Middlewares/ST/STM32_USB_Device_Library/
```

Do not add these application-only parts:

- FreeRTOS and CMSIS-RTOS2;
- `app_freertos.c` and `FreeRTOSConfig.h`;
- `Task<>`, `DebugService`, `AstroWeather`, or application variants;
- SPI1, unless later required by a documented ST67 programming procedure;
- switch and application LED interrupts, unless deliberately used for mode selection/status.

C is sufficient for this project. Use C11 and avoid enabling C++ unless another project requirement needs it.

## CubeMX Configuration

The project is already bootstrapped, so do not recreate it. Verify its MCU is the exact device used by the board:

```text
STM32G0B1CET6, LQFP48
STM32CubeG0 firmware package 1.6.3
Toolchain: CMake
```

Keep the already-working USB CDC configuration. Add or verify only the settings below. If the existing project was generated from an `.ioc`, make peripheral changes there and regenerate so the CMake source inventory remains consistent.

### Clock tree

Verify the clock configuration:

- HSI: 16 MHz, enabled;
- HSI48: enabled for USB;
- PLL source: HSI;
- PLLM: /1;
- PLLN: x8;
- PLLR: /2;
- SYSCLK/HCLK/PCLK1: 64 MHz;
- USB clock: HSI48 at 48 MHz;
- voltage scaling: Scale 1;
- Flash latency: 2.

Use SysTick as the HAL time base in this non-RTOS project.

### USB

- Preserve the existing USB DRD FS device-only configuration.
- Preserve the existing USB Device CDC Full Speed middleware and `USB_UCPD1_2_IRQn`.
- Keep the existing generated `usb_device.c`, `usbd_desc.c`, `usbd_cdc_if.c`, and `usbd_conf.c` files.
- Keep the descriptors if the echo device already enumerates correctly; optionally use a product name that clearly identifies this utility firmware.

The generated USB interrupt handler must call:

```c
void USB_UCPD1_2_IRQHandler(void)
{
    HAL_PCD_IRQHandler(&hpcd_USB_DRD_FS);
}
```

### USART2

Configure PA2/PA3 for USART2 asynchronous TX/RX with the proven format above. Enable `USART2_LPUART2_IRQn` and choose an interrupt priority that cannot be blocked by USB processing for long periods. Without FreeRTOS, priority 1 is suitable; USB can use the generated default priority.

After `HAL_UART_Init`, enable FIFO mode and RX interrupts:

```c
if (HAL_UARTEx_SetTxFifoThreshold(&huart2, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK) {
    Error_Handler();
}
if (HAL_UARTEx_SetRxFifoThreshold(&huart2, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK) {
    Error_Handler();
}
if (HAL_UARTEx_EnableFifoMode(&huart2) != HAL_OK) {
    Error_Handler();
}

__HAL_UART_ENABLE_IT(&huart2, UART_IT_RXFNE);
HAL_NVIC_SetPriority(USART2_LPUART2_IRQn, 1, 0);
HAL_NVIC_EnableIRQ(USART2_LPUART2_IRQn);
```

Cube/HAL naming differs slightly between package versions. The direct register equivalent is:

```c
huart2.Instance->CR1 |= USART_CR1_RXNEIE_RXFNEIE;
```

### GPIO startup safety

Set output levels before configuring pins as outputs to prevent glitches. The safe initial state is `CHIP_EN=0`; set `CS` and `BOOT` to the desired strap values before enabling the ST67.

## Firmware Startup

The bare-metal startup order should be:

```c
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART2_UART_Init();
    Bridge_Init();
    MX_USB_Device_Init();
    ST67_EnterMode(ST67_MODE_MANUFACTURE); /* or verified bootloader mode */

    while (1) {
        Bridge_Process();
    }
}
```

`Bridge_Init` must reset ring indices and enable the USART2 RX interrupt. Initialize USB before enabling the ST67, but keep `CHIP_EN` low until all ST67 strap outputs are configured. USB initialization does not mean that enumeration is complete or that the PC has opened the CDC port. The UART RX ring must therefore be large enough to retain startup output, or the design must provide a deliberate hardware reset action.

## Echo-to-Bypass Migration

Make the change in this order so the known-good USB path remains easy to test:

1. Confirm the unmodified project still echoes arbitrary binary USB packets using their explicit lengths.
2. Add the ST67 GPIO labels and USART2 configuration described above.
3. Add `bridge.h` and `bridge.c`, then include `bridge.c` in the existing CMake executable target.
4. Call `Bridge_Init()` after UART initialization.
5. Replace the echo operation in `CDC_Receive_FS` with `Bridge_OnUsbRx(buffer, *length)` and immediately rearm the OUT endpoint.
6. Wire `CDC_TransmitCplt_FS` to `Bridge_OnUsbTxComplete()`.
7. Replace the empty foreground loop with repeated `Bridge_Process()` calls.
8. Add the dedicated `USART2_LPUART2_IRQHandler` shown below.
9. Add `ST67_EnterMode()` and call it only after GPIO, UART, bridge, and USB initialization.
10. Rebuild and first retest USB enumeration, then test each bridge direction independently.

Remove all original echo state and echo transmission calls. After migration, `CDC_Receive_FS` must enqueue PC bytes for USART2; it must never send those bytes directly back to the PC.

For predictable operator behavior, a useful design is:

1. initialize USB and UART with ST67 disabled;
2. select manufacture or bootloader mode using a compile-time option or physical button;
3. enable ST67 immediately, without waiting for the PC CDC port to open;
4. optionally provide a hardware button or USB control command that repeats the mode-entry reset sequence.

Do not put bridge-control commands in the same byte stream unless there is an explicit escape/framing protocol. Arbitrarily recognizing text commands would make the bridge non-transparent and could corrupt bootloader data.

## Bridge Architecture

Use two single-producer/single-consumer paths:

```text
USB OUT callback -> PC-to-ST67 ring -> foreground -> USART2 TDR
USART2 RX ISR    -> ST67-to-PC ring -> foreground -> USB CDC IN
```

Recommended initial sizes:

```c
#define USB_TO_UART_RING_SIZE  512u
#define UART_TO_USB_RING_SIZE  4096u
#define USB_TX_BUFFER_SIZE     256u
```

The public interface in `bridge.h` is:

```c
#ifndef BRIDGE_H
#define BRIDGE_H

#include <stdint.h>

void Bridge_Init(void);
void Bridge_Process(void);
void Bridge_OnUsbRx(const uint8_t *data, uint32_t length);
void Bridge_OnUsbTxComplete(void);

#endif
```

Define the complete bridge state in `bridge.c`; none of it comes from another project:

```c
#include "bridge.h"

#include <stdbool.h>

#include "main.h"
#include "usbd_cdc_if.h"

extern UART_HandleTypeDef huart2;

#define USB_TO_UART_RING_SIZE  512u
#define UART_TO_USB_RING_SIZE  4096u
#define USB_TX_BUFFER_SIZE     256u

static uint8_t usb_to_uart_data[USB_TO_UART_RING_SIZE];
static volatile uint32_t usb_to_uart_head;
static volatile uint32_t usb_to_uart_tail;

static uint8_t uart_to_usb_data[UART_TO_USB_RING_SIZE];
static volatile uint32_t uart_to_usb_head;
static volatile uint32_t uart_to_usb_tail;

static uint8_t usb_tx_buffer[USB_TX_BUFFER_SIZE];
static volatile bool usb_tx_busy;

static volatile uint32_t usb_to_uart_overflow_count;
static volatile uint32_t uart_to_usb_overflow_count;
static volatile uint32_t uart_hardware_overrun_count;
```

Initialize the state and USART2 RX interrupt explicitly:

```c
void Bridge_Init(void)
{
    usb_to_uart_head = 0u;
    usb_to_uart_tail = 0u;
    uart_to_usb_head = 0u;
    uart_to_usb_tail = 0u;
    usb_tx_busy = false;
    usb_to_uart_overflow_count = 0u;
    uart_to_usb_overflow_count = 0u;
    uart_hardware_overrun_count = 0u;

    __HAL_UART_ENABLE_IT(&huart2, UART_IT_RXFNE);
    HAL_NVIC_SetPriority(USART2_LPUART2_IRQn, 1u, 0u);
    HAL_NVIC_EnableIRQ(USART2_LPUART2_IRQn);
}
```

Ring sizes must be powers of two if indexing uses a bit mask. Use monotonically increasing unsigned `head` and `tail` counters; occupancy is `head - tail`, and the storage index is `counter & (size - 1)`. A 4 KiB UART RX ring holds about 20 ms at 2 Mbaud, 8-N-1. Increase it if the host or USB stack can stall longer.

Declare indices shared with an ISR as `volatile`. On this Cortex-M0+ target, aligned 32-bit loads/stores are atomic. Preserve the single-producer/single-consumer ownership:

| Object | Producer | Consumer |
|---|---|---|
| USB-to-UART head/data | USB receive callback | foreground loop |
| USB-to-UART tail | foreground loop | foreground loop |
| UART-to-USB head/data | USART2 ISR | foreground loop |
| UART-to-USB tail | foreground loop | foreground loop |

Track overflow counters for both rings. On overflow, drop the incoming byte and increment the corresponding counter. Do not silently overwrite unread data because that destroys ordering and makes bootloader failures harder to diagnose.

## USART2 Interrupt Handler

At 2 Mbaud, byte-by-byte `HAL_UART_Receive` in the foreground loses data. The ISR must immediately drain every available FIFO byte:

```c
void USART2_LPUART2_IRQHandler(void)
{
    USART_TypeDef *uart = huart2.Instance;

    while ((uart->ISR & USART_ISR_RXNE_RXFNE) != 0u) {
        uint8_t byte = (uint8_t)(uart->RDR & 0xffu);
        uint32_t head = uart_to_usb_head;

        if ((head - uart_to_usb_tail) < UART_TO_USB_RING_SIZE) {
            uart_to_usb_data[head & (UART_TO_USB_RING_SIZE - 1u)] = byte;
            uart_to_usb_head = head + 1u;
        } else {
            ++uart_to_usb_overflow_count;
        }
    }

    if ((uart->ISR & USART_ISR_ORE) != 0u) {
        __HAL_UART_CLEAR_OREFLAG(&huart2);
        ++uart_hardware_overrun_count;
    }
}
```

Do not call the generic `HAL_UART_IRQHandler` after manually reading `RDR` unless the HAL receive state machine is deliberately in use. This bridge owns RX directly.

## USB Receive Path

The generated `CDC_Receive_FS` callback runs from USB processing context. Copy the packet immediately because the middleware reuses its receive buffer after rearming the endpoint:

```c
static int8_t CDC_Receive_FS(uint8_t *buffer, uint32_t *length)
{
    Bridge_OnUsbRx(buffer, *length);

    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, buffer);
    USBD_CDC_ReceivePacket(&hUsbDeviceFS);
    return USBD_OK;
}
```

`Bridge_OnUsbRx` only appends bytes to the USB-to-UART ring. It must not block waiting for UART TX and must not retain the middleware's `buffer` pointer.

```c
void Bridge_OnUsbRx(const uint8_t *data, uint32_t length)
{
    for (uint32_t index = 0u; index < length; ++index) {
        uint32_t head = usb_to_uart_head;
        if ((head - usb_to_uart_tail) >= USB_TO_UART_RING_SIZE) {
            ++usb_to_uart_overflow_count;
            continue;
        }

        usb_to_uart_data[head & (USB_TO_UART_RING_SIZE - 1u)] = data[index];
        usb_to_uart_head = head + 1u;
    }
}
```

## UART Transmit Path

The foreground loop can initially use direct TX FIFO polling. RX remains safe because the higher-priority USART ISR can interrupt this loop:

```c
static void bridge_usb_to_uart(void)
{
    while (usb_to_uart_tail != usb_to_uart_head) {
        while ((huart2.Instance->ISR & USART_ISR_TXE_TXFNF) == 0u) {
        }

        huart2.Instance->TDR =
            usb_to_uart_data[usb_to_uart_tail & (USB_TO_UART_RING_SIZE - 1u)];
        ++usb_to_uart_tail;
    }
}
```

This is adequate for the first implementation. UART TX interrupt or DMA can be added later if foreground blocking becomes measurable.

## USB Transmit Ownership

This point is critical: the USB middleware does not copy the application TX buffer. It retains the pointer until the IN transfer completes. A buffer must not be refilled merely because `CDC_Transmit_FS` returned `USBD_OK`.

Use a persistent buffer and an explicit busy flag:

```c
static uint8_t usb_tx_buffer[USB_TX_BUFFER_SIZE];
static volatile bool usb_tx_busy;

static void bridge_uart_to_usb(void)
{
    if (usb_tx_busy || uart_to_usb_tail == uart_to_usb_head) {
        return;
    }

    uint16_t length = 0u;
    while (uart_to_usb_tail != uart_to_usb_head &&
           length < sizeof(usb_tx_buffer)) {
        usb_tx_buffer[length++] =
            uart_to_usb_data[uart_to_usb_tail & (UART_TO_USB_RING_SIZE - 1u)];
        ++uart_to_usb_tail;
    }

    usb_tx_busy = true;
    if (CDC_Transmit_FS(usb_tx_buffer, length) != USBD_OK) {
        usb_tx_busy = false;
        uart_to_usb_tail -= length;
    }
}
```

Clear the flag only from the generated transmit-complete callback:

```c
static int8_t CDC_TransmitCplt_FS(uint8_t *buffer,
                                  uint32_t *length,
                                  uint8_t endpoint)
{
    (void)buffer;
    (void)length;
    (void)endpoint;
    Bridge_OnUsbTxComplete();
    return USBD_OK;
}

void Bridge_OnUsbTxComplete(void)
{
    usb_tx_busy = false;
}
```

Before dereferencing CDC class state, harden the generated helper for startup/disconnect:

```c
uint8_t CDC_Transmit_FS(uint8_t *buffer, uint16_t length)
{
    if (hUsbDeviceFS.pClassData == NULL) {
        return USBD_BUSY;
    }

    USBD_CDC_HandleTypeDef *cdc =
        (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
    if (cdc->TxState != 0u) {
        return USBD_BUSY;
    }

    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, buffer, length);
    return USBD_CDC_TransmitPacket(&hUsbDeviceFS);
}
```

The rollback in `bridge_uart_to_usb` is safe only because that function is the sole consumer of the UART-to-USB ring. An alternative is to advance `tail` only after `CDC_Transmit_FS` accepts the transfer.

## Foreground Processing

The process function should be short and continuously callable:

```c
void Bridge_Process(void)
{
    bridge_usb_to_uart();
    bridge_uart_to_usb();
}
```

Do not call `HAL_Delay` in the normal bridge loop. Delays are acceptable only during a deliberate ST67 reset/mode-entry sequence. Avoid logging over the same CDC interface because diagnostic text would be indistinguishable from ST67 data. Expose overflow information through a debugger, LEDs, or a separate explicitly framed control interface.

## Binary Transparency Requirements

The following transformations are forbidden in both directions:

- ANSI escape removal;
- CR/LF conversion;
- null termination;
- printable-character filtering;
- command echo;
- UTF-8 or text decoding;
- packet-boundary assumptions;
- use of `strlen` on received data.

USB packets and UART reads are fragments of a byte stream. Forward exactly the received byte count, including `0x00`, `0x1b`, and bytes above `0x7f`.

## CMake Requirements

Do not replace the working CMake setup. Confirm its existing STM32CubeMX target already contains:

- startup assembly for STM32G0B1;
- `main.c`, interrupt, MSP, syscall, and system sources;
- USB application and target sources;
- USB Device Core sources;
- USB CDC class source;
- required HAL sources for GPIO, RCC, PWR, UART, PCD, and USB LL;
- CMSIS and HAL include directories;
- USB Device Core and CDC include directories;
- compile definitions `USE_HAL_DRIVER` and `STM32G0B1xx`.

It must not contain FreeRTOS sources, CMSIS-RTOS2 include paths, `app_freertos.c`, or a `FreeRTOS` object library.

A typical top-level addition is only the new source:

```cmake
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Src/bridge.c
)
```

Keep the already-working compiler, linker script, presets, and artifact generation unchanged. No new library dependency is required.

## Mode Selection Design

Represent modes explicitly even if bootloader details are initially unavailable:

```c
typedef enum {
    ST67_MODE_MANUFACTURE,
    ST67_MODE_BOOTLOADER
} St67Mode;

void ST67_EnterMode(St67Mode mode);
```

Implement manufacture mode now with the proven levels. For bootloader mode, fail at compile time or leave it unavailable until its strap/timing values are verified; do not silently reuse manufacture-mode levels.

Reasonable ways to select the mode are:

- separate CMake presets that define `ST67_DEFAULT_MODE`;
- a dedicated physical button sampled before raising `CHIP_EN`;
- two separately named firmware artifacts from the same source tree.

A compile-time selection is simplest and cannot collide with bridged payload data. Example:

```cmake
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    ST67_DEFAULT_MODE=ST67_MODE_MANUFACTURE
)
```

## Host-Side Behavior

The PC serial application's selected baud rate does not control USB CDC transfer speed. The STM32 should report ordinary line coding through the CDC control callback but keep physical USART2 fixed at the mode-specific ST67 baud unless dynamic baud changes are an intentional feature.

For manufacture mode, test first with a terminal that can open the VCP without changing payload bytes. The expected shell prompt is `qcc74x />`. A terminal can send `help` followed by CR/LF to confirm bidirectional operation.

For bootloader mode, use the vendor programming tool or protocol client. A terminal is not a meaningful validation because the protocol may be binary.

## Bring-Up and Validation Checklist

1. Build with no FreeRTOS or CMSIS-RTOS2 references.
2. Flash the dedicated firmware and confirm the CDC device enumerates with its distinct product name.
3. Confirm PA2 with an oscilloscope or logic analyzer: 2 Mbaud, 8-N-1.
4. Confirm manufacture entry levels during the rising edge of `CHIP_EN`: `CS=1`, `BOOT=0`.
5. Open the CDC port and verify the manufacture prompt/traffic is received.
6. Send `help\r\n` and verify exact byte-for-byte UART output and response forwarding.
7. Send a payload containing `00 1b 7f 80 ff` in both directions and verify no byte is filtered or changed.
8. Send sustained data at the maximum expected rate and watch all three counters: USB-to-UART overflow, UART-to-USB overflow, and UART hardware overrun. All must remain zero.
9. Disconnect USB while ST67 transmits, reconnect, and verify firmware remains responsive. Buffer overflow during a long disconnect is expected and must be counted, not deadlock the bridge.
10. Repeatedly reset or power-cycle ST67 and verify mode entry is deterministic.
11. After bootloader straps and protocol are documented, validate bootloader entry electrically before running the vendor tool.
12. Program a known image, verify it, reset into the intended post-program mode, and confirm the image boots.

## Acceptance Criteria

The project is complete when:

- it builds and links as a standalone STM32G0B1 CMake firmware without FreeRTOS;
- PC-to-ST67 and ST67-to-PC forwarding is byte-exact;
- continuous 2 Mbaud ST67 output causes no UART hardware overruns under normal connected-host operation;
- USB TX memory is not modified until transmit completion;
- manufacture mode enters reliably using the known levels;
- bootloader mode is enabled only after its pin levels, timing, UART settings, and protocol are verified from authoritative ST67 documentation;
- no application source or application debug/logging dependency is linked into the utility firmware.
