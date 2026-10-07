# Firmware RAM Usage

Where the HostController's RAM goes, from the Debug build of 2026-10-07
(`build/Debug/HostControllerA.elf`, GNU Tools for STM32 14.3.1). The ST67
module runs the T01 firmware, so TCP/IP and TLS are in the module and no
network stack is linked on the host; see [WiFi.md](WiFi.md). Earlier
measurements, including the T02 configuration with LwIP, and the reduction
plan are in [archive/RAM_Usage_History.md](archive/RAM_Usage_History.md).

## Summary

| Item | Bytes |
| --- | ---: |
| RAM capacity, STM32G0B1CETx | 147456 (144 KiB): the 139 KiB `RAM` region plus the 5 KiB `NOINIT` region |
| `.data` | 644 |
| `.bss` | 90448 |
| `._user_heap_stack`: C heap `0x200` + main stack `0x400` | 1536 |
| **`RAM` region in use**, as the link reports it | **92632** of 142336 (65.1%) |
| **`RAM` region left** | **49704** |
| `.noinit` (`NOINIT` region) | 4364 of 5120 |

The `NOINIT` region at the top of RAM holds the error log
(`ErrorLog::Storage`, 24 entries of 160-byte text). The startup code does not
clear it, so the log survives a reset; see [Console.md](Console.md#error-log).
The linker script change that creates it is described in
[Development.md](../../Docs/Development.md).

The figures are address-space reservation at link time, not use at any one
moment. 32 000 B of it is the FreeRTOS heap, whose use is only visible at run
time; see [FreeRTOS heap](#freertos-heap).

## Largest RAM Reservations

From `arm-none-eabi-nm -S --size-sort` on the same ELF:

| Allocation | Bytes | Owner / source |
| --- | ---: | --- |
| FreeRTOS heap (`ucHeap`) | 32000 | `configTOTAL_HEAP_SIZE` in `Core/Inc/FreeRTOSConfig.h` |
| `St67HttpFetchTask` object | 8928 | 4096-byte stack and `St67Runtime`, including its own 4096-byte `httpPayload`, and the fetcher's `ApiTarget`; `User/Src/WiFi/St67HttpFetchTask.cpp` |
| `AstroDataRefreshTask` object | 8040 | 3328-byte stack, 4096-byte response buffer and state; `User/Inc/Astro/AstroDataRefreshTask.hpp` |
| `LogService` object | 6464 | 1536-byte stack and a 16-entry queue of 257-byte events; `User/Inc/Debug/LogService.hpp` |
| Error log (`errorLogStorage`, `NOINIT`) | 4364 | `User/Src/Debug/LogService.cpp` |
| Error log copy for `errors` (`snapshot`) | 4364 | `User/Src/Console/ErrorsCommand.cpp` |
| `ConsoleService` object | 4256 | 2304-byte stack, 8-entry command queue of 128-byte lines, 256-byte RX ring; `User/Inc/Console/ConsoleService.hpp` |
| USB CDC buffers | 4096 | `UserRxBufferFS` and `UserTxBufferFS`, 2048 bytes each |
| `CurrentSenseTask` object | 2480 | 2048-byte stack |
| `MainLoopTask` object | 2480 | 2048-byte stack |
| `ClockTask` object | 1600 | 1024-byte stack |
| `DisplaySyncTask` object | 2528 | 2048-byte stack, the sync schedule and the boards' last status; `User/Src/Display/DisplaySyncTask.cpp` |
| USB device state | about 2000 | `hpcd_USB_DRD_FS` 736, `hUsbDeviceFS` 732, `USBD_StrDesc` 512 |
| FreeRTOS static support | about 3400 | Idle stack 512, timer stack 1024, their TCBs 384 each, ready lists 1120 |
| ST67 static driver state | about 1200 | `W61_Obj` 992 bytes, the SPI transfer engine state |
| `localBoard` (`PcbDisplayBoard`) | 968 | Content and attributes, two 280-byte sets of prepared pass frames, the sequencer, the timeline servo and its two frame records |
| `settingsStore` (`Settings::Store`) | 844 | `Values` (including the 65-byte API host and path), two 256-byte working images for `load()`/`save()`, the mutex; `User/Inc/Settings/SettingsStore.hpp` |

The same astro payload is held twice: once in `St67Runtime::httpPayload`,
which only the stress batches write, and once in
`AstroDataRefreshTask::responseBuffer_`, which every refresh uses. Holding it
once would save 4 KB; it is listed with the open items in the
[README](../README.md).

## FreeRTOS Usage

### FreeRTOS heap

`configTOTAL_HEAP_SIZE` is 32 000 B, for `heap_4.c`. It holds `defaultTask`
until it exits after starting USB, the CMSIS objects created without static
memory, the ST67 driver's two tasks and its queues, event group and transfer
buffers, and the HTTP client's per-request buffers: a 2 KiB header buffer, a
1 KiB read buffer and a 512-byte request, plus the driver's socket state.
Application tasks and their stacks are static.

Measured on the bench with this heap size:

| State | Free |
| --- | ---: |
| Before the module starts | about 31 000 B |
| Module and its two tasks up, idle | about 21 400 B |
| Lowest through an HTTPS fetch (`heapMin`) | about 16 400 B |

The peak demand is about 15.9 KB, on a re-initialisation after `stop()`,
which leaves about twice an 8 KiB margin; it stays flat over 100 HTTPS cycles.
Reduce the heap further only after measuring `heapMin` under the worst-case
WiFi workload: connect, DHCP, HTTPS, the certificate upload, failure paths
and a restart after `stop()`.

### Static reservation

- Idle task stack: `512` bytes (`configMINIMAL_STACK_SIZE` 128 words)
- Timer task stack: `1024` bytes (`configTIMER_TASK_STACK_DEPTH` 256 words)
- Idle and timer TCBs: `384` bytes each
- Ready-task lists and scheduler state: about `1120` bytes
- The application task objects listed above, whose stacks are static

### Runtime diagnostics

`stats on` makes `LogService::emitStats()` report periodically:

```text
[STATS] sent=<lines> dropped=<lines> busyDrop=<lines>
[MEM] heapFree=<current free bytes> heapMin=<minimum-ever free bytes>
[STACK] name=<task> configured=<configured bytes> remaining=<high-water bytes>
```

`[STACK]` covers the application's `Task<>` objects only, not `defaultTask`
or the middleware tasks. Reduce a stack only after checking its high-water
mark under its deepest call path. `log()` and `logf()` build their 257-byte
line on the caller's stack, so every task that logs needs that headroom.

### Tasks and configured stacks

Application tasks, with static stacks inside their objects:

| Task (name in `[STACK]`) | Stack | Priority | Source |
| --- | ---: | --- | --- |
| `St67HttpFetch` | 4096 | BelowNormal | `User/Src/WiFi/St67HttpFetchTask.cpp`; the W6X socket path and the driver's AT trace run on it |
| `AstroDataRefresh` | 3328 | Normal | `AstroDataRefreshTask.hpp`; the clock sync and the logging run on it |
| `ConsoleService` | 2304 | Normal | `ConsoleService.hpp`; `status` is its deepest command, about 490 B left |
| `CurrentSense` | 2048 | BelowNormal | `CurrentSenseTask.hpp` |
| `MainLoopTask` | 2048 | Normal | `MainLoopTask.hpp`; the boot screens encode frames on it, 944 used at peak |
| `LogService` | 1536 | Normal | `LogService.hpp` |
| `Clock` | 1024 | BelowNormal | `ClockTask.hpp` |
| `DisplaySync` | 2048 | AboveNormal | `DisplaySyncTask.hpp`; 1008 used after a logged burst and a logged content re-send (bench, 2026-10-07) |

Generated and middleware tasks, with stacks from the FreeRTOS heap unless
noted:

| Task | Stack | Source |
| --- | ---: | --- |
| `defaultTask` | 512 | `Core/Src/main.c`, `128 * 4`; freed when it exits after starting USB |
| FreeRTOS idle | 512 | static, see above |
| FreeRTOS timer | 1024 | static, see above |
| ST67 modem RX task | 2048 | `W61_MDM_RX_TASK_STACK_SIZE_BYTES` in the ST67 driver |
| ST67 SPI transfer engine | 1536 | `SPI_THREAD_STACK_SIZE` in `ST67W6X_Network_Driver/Target/w61_driver_config.h` |

The driver's own HTTP client (`w6x_http.c`) is not linked: the firmware uses
its own `User/Src/WiFi/HttpClient.cpp` on the fetch task. `St67ProbeTask`
(`Task<2048>`) is compiled but has no instance in this image.

## ST67 Middleware Usage

ST67 has two kinds of RAM cost:

1. Static driver state linked into `.bss`, about 1200 bytes including the
   992-byte `W61_Obj`.
2. Dynamic allocations from the shared FreeRTOS heap: the modem RX task and
   its receive buffer (`W61_MAX_SPI_XFER = 1520` bytes), the SPI transfer
   task, queues, event group and transfer buffers, scan results, and socket
   state. The module up and idle costs about 10 KB of the heap, a fetch about
   5 KB more.

The dynamic allocations are not visible in the ELF; `heapFree` and `heapMin`
show them.

## Verification Commands

From Git Bash, with the toolchain on `PATH` (see
[Development.md](../../Docs/Development.md)):

```bash
arm-none-eabi-size -A -d build/<preset>/HostControllerA.elf
arm-none-eabi-nm -S --size-sort --radix=d -C build/<preset>/HostControllerA.elf
```

The most important link-time values are `.data`, `.bss`, `.noinit`,
`._user_heap_stack` and `ucHeap`. `arm-none-eabi-size -B` reports `bss`
including `._user_heap_stack`. Compare the link-time results again after every
RAM configuration change.
