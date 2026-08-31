# Firmware RAM Usage

## Summary

Measured from the `Debug-HostController` linked image on 2026-08-30:

- MCU: STM32G0B1CETx
- RAM capacity: 144 KiB (`147456` bytes)
- `.data + .bss`: `131564` bytes
- Linker-reserved C heap and main stack: `1540` bytes
- Total statically reserved RAM: `133104` bytes
- Remaining RAM after static reservation: `14352` bytes
- Static RAM usage: approximately `90.3%`

The linked image is close to the RAM limit. The percentage describes address-space reservation at link time; it does not mean every byte is actively in use at every moment.

The current ELF and map files are generated under `build/Debug-HostController/`:

- `HostControllerA.elf`: linked firmware image
- `HostControllerA.map`: linker map with section and symbol ownership

## Largest RAM Reservations

| Allocation | Bytes | Owner / source |
| --- | ---: | --- |
| FreeRTOS heap (`ucHeap`) | 40000 | `configTOTAL_HEAP_SIZE` in `Core/Inc/FreeRTOSConfig.h` |
| LwIP heap (`ram_heap`) | 33551 | `MEM_SIZE` calculated in `LWIP/Target/lwipopts.h` |
| Debug service object | 15616 | 1536-byte task stack plus 64-entry log queue and state in `User/Inc/Debug/DebugService.hpp` |
| ST67 HTTP fetch task object | 7200 | 2560-byte task stack plus task state in `User/Src/HostController/St67HttpFetchTask.cpp` |
| Main-loop task object | 6056 | 1536-byte task stack plus task state in `User/Inc/HostController/MainLoopTask.hpp` |
| USB CDC buffers | 4096 | `UserRxBufferFS` and `UserTxBufferFS`, 2048 bytes each |
| Switch task object | 1960 | 1536-byte task stack plus task state |
| FreeRTOS scheduler/static support | approximately 3424 | Idle/timer stacks, TCBs, and ready-task lists |
| LwIP pools and tables | approximately 9420 | TCP pools, DNS table, IPv6 caches, netif state, and statistics |
| ST67 static driver state | 1184 | Includes `W61_Obj` at 992 bytes |

The totals above are grouped by linker symbols and are not all additive with the broad subsystem labels below. For example, task objects are application-owned static allocations, while their dynamically created ST67/LwIP tasks consume the shared FreeRTOS heap at runtime.

## FreeRTOS Usage

### Static reservation

FreeRTOS-related static RAM currently includes:

- FreeRTOS dynamic heap: `40000` bytes
- Idle task stack: `512` bytes
- Timer task stack: `1024` bytes
- Idle and timer TCBs: `384` bytes each
- Ready-task lists and scheduler state: approximately `1120` bytes
- Application task stacks and objects listed above

`configTOTAL_HEAP_SIZE` is a reservation for `heap_4.c`; it is not the same as the heap currently allocated. The heap is shared by CMSIS-RTOS2 objects, ST67, LwIP, and application code.

Runtime diagnostics are emitted by `DebugService::emitStats()`:

```text
[MEM] heapFree=<current free bytes> heapMin=<minimum-ever free bytes>
[STACK] name=<task> configured=<configured bytes> remaining=<high-water bytes>
```

`heapMin` measured during a worst-case ST67 connect, DHCP, HTTP, and TLS workload is the value to use when deciding whether `configTOTAL_HEAP_SIZE` can be reduced. Every task stack should also be reduced only after checking its high-water mark under its deepest call path.

### FreeRTOS tasks and configured stacks

| Task / object | Configured stack |
| --- | ---: |
| `DebugService` | 1536 bytes |
| `MainLoopTask` | 1536 bytes |
| `SwitchTask` | 1536 bytes |
| `ConsoleService` | 1024 bytes |
| `BlinkingLed` | 768 bytes |
| `St67HttpFetchTask` | 2560 bytes |
| ST67 SPI task | 1536 bytes via `SPI_THREAD_STACK_SIZE` |
| ST67 netif task | 2048 bytes, dynamically created |
| LwIP TCP/IP task | 4096 bytes, dynamically created |
| LwIP HTTP task | 1536 bytes when used |
| FreeRTOS timer task | 256 words in `FreeRTOSConfig.h` |

The ST67 and LwIP task stacks created with `xTaskCreate()` are allocated from the shared FreeRTOS heap. Their stack-size values are expressed in bytes by the surrounding code and converted to FreeRTOS stack words where required.

## LwIP Usage

LwIP reserves `33551` bytes for its heap. This comes from:

```c
#define PBUF_LINK_ENCAPSULATION_HLEN 388
#define FACTOR 64
#define MEM_MIN (2300 + FACTOR * (100 + PBUF_LINK_ENCAPSULATION_HLEN))
```

That calculates to `33552` bytes before alignment; the linked symbol is `33551` bytes.

Additional linked LwIP pools and tables use approximately `9420` bytes. The current configuration also enables or sizes several high-water features:

- TCP receive window: `22 * TCP_MSS`, approximately `32120` bytes of protocol window capacity
- TCP send buffer: `16 * TCP_MSS`, approximately `23360` bytes
- TCP segment and pbuf pools sized from the send buffer
- `TCPIP_MBOX_SIZE`: `64`
- Raw/UDP/TCP/accept mailbox sizes: `32`, `64`, `64`, and `32`
- IPv6, MLD, ND, IPv6 forwarding, and route-table support
- DHCP, DNS, IGMP, packet reassembly, and socket APIs

The TCP window and send-buffer values are protocol capacities; they are not all directly reserved as one contiguous RAM allocation. They nevertheless drive LwIP pool counts and can increase the amount of RAM required during traffic bursts.

## ST67 Middleware Usage

ST67 has two kinds of RAM cost:

1. Static driver state linked into `.bss`.
2. Dynamic allocations made from the shared FreeRTOS heap.

The measured static ST67 driver state is approximately `1184` bytes, including the `992`-byte `W61_Obj`. ST67 also creates or allocates resources such as:

- Modem command task and receive buffer (`W61_MAX_SPI_XFER = 1520` bytes)
- SPI task, queues, event group, and transfer buffers
- Netif task with a `2048`-byte stack
- Wi-Fi scan-result storage
- Network context and socket/certificate state
- Temporary HTTP request/response buffers
- Optional TLS state and certificate buffers

These dynamic allocations are not identifiable as separate fixed blocks in the ELF because they come from `ucHeap` at runtime. Their effect is captured by the `heapFree` and `heapMin` diagnostics.

The ST67 source set also includes its complete W61 AT/Core implementation and LwIP integration. Code inclusion mainly affects flash; only global/static objects and runtime allocations affect RAM.

## Reduction Plan

Apply reductions in this order and validate each step with the complete application workload.

### 1. Reduce the debug queue

`DebugService` currently stores 64 events, each with a 200-byte text buffer. The queue storage is approximately `12864` bytes and is the largest easily removable application allocation.

Reducing the queue depth from 64 to 16 would recover approximately:

```text
48 * sizeof(LogEvent) = 48 * 201 = 9648 bytes
```

The existing overflow policy drops the oldest event, so this change does not block producers. It does reduce burst tolerance for debug output.

### 2. Measure before reducing the FreeRTOS heap

Run the full ST67 workflow, including association, DHCP, HTTP, repeated requests, and TLS if TLS is required. Record the lowest `heapMin` value. Reduce `configTOTAL_HEAP_SIZE` only when that value leaves an explicit margin for error paths and future changes.

Do not infer required heap from the current free value after initialization; temporary HTTP, TLS, and scan allocations can produce a lower watermark later.

### 3. Reduce LwIP capacities

The largest candidates are:

- `FACTOR` in `LWIP/Target/lwipopts.h`
- `TCP_WND`
- `TCP_SND_BUF`
- TCP segment/pbuf counts derived from `TCP_SND_BUF`
- TCP/IP and API mailbox depths

Reducing `FACTOR` from 64 to 48 would save roughly `7.8 KiB` of LwIP heap. Reducing it to 32 would save roughly `15.6 KiB`. These changes must be tested with the maximum expected packet size and concurrent traffic.

### 4. Tune task stacks from high-water marks

Use the `[STACK]` diagnostics after exercising the deepest path. Reduce a stack only when the observed margin remains comfortably above the largest interrupt, library, formatting, and error-path requirements. `DebugService::logf()` uses formatted output and has already required substantial stack headroom in callers.

### 5. Disable unused network features

If the product does not need them, IPv6/MLD/ND, IGMP, packet reassembly, AP support, unused socket features, and unused PPP sources can be removed or disabled. Feature removal should be done through the applicable LwIP/ST67 configuration and regenerated project settings, not by deleting generated source files ad hoc.

## Verification Commands

From Git Bash, using the toolchain already available on `PATH`:

```bash
arm-none-eabi-size -A -d build/Debug-HostController/HostControllerA.elf
arm-none-eabi-nm -S --size-sort --radix=d build/Debug-HostController/HostControllerA.elf
```

The most important link-time values are `.data`, `.bss`, `._user_heap_stack`, `ucHeap`, and `ram_heap`. Link-time results should be compared again after every RAM configuration change.

## Current Recommendation

The first low-risk change is reducing the debug queue from 64 to 16, which should lower static RAM reservation by approximately `9648` bytes without changing ST67 or LwIP behavior. After that, use the runtime `heapMin` and stack high-water reports from the worst-case workload before reducing the FreeRTOS heap, LwIP heap, or task stacks.
