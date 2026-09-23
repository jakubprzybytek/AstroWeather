# Firmware RAM Usage

## Summary

Measured on 2026-09-23 from the Debug HostController build in
`build/claude-host/` (`HostControllerA.elf` linked 2026-09-23 12:43; it
includes the scheduled astro refresh committed in `03f2ebd`), using
`arm-none-eabi-size` from GNU Tools for STM32 14.3.1:

| Item | Bytes |
| --- | ---: |
| RAM capacity, STM32G0B1CETx | 147456 (144 KiB) |
| `.data` | 616 |
| `.bss` | 132016 |
| `._user_heap_stack`: C heap `0x200` + main stack `0x400` | 1536 |
| **Total statically reserved** | **134168** |
| **Remaining** | **13288** |
| Static RAM usage | about **91.0%** |

The previous measurement, on 2026-08-30 from `build/Debug-HostController/`,
was 131564 bytes of `.data + .bss` and 14352 bytes remaining, so static use
has grown by about 1 KB since.

The percentage describes address-space reservation at link time; it does not
mean every byte is in use at every moment. About 73.5 KB of it is two heaps,
FreeRTOS and LwIP, whose use is only visible at run time.

## Largest RAM Reservations

From `arm-none-eabi-nm -S --size-sort` on the same ELF:

| Allocation | Bytes | Owner / source |
| --- | ---: | --- |
| FreeRTOS heap (`ucHeap`) | 40000 | `configTOTAL_HEAP_SIZE` in `Core/Inc/FreeRTOSConfig.h` |
| LwIP heap (`ram_heap`) | 33551 | `MEM_SIZE` calculated in `LWIP/Target/lwipopts.h` |
| `AstroDataRefreshTask` object | 7704 | 3072-byte stack, 4096-byte response buffer and state; `User/Inc/HostController/AstroDataRefreshTask.hpp` |
| `St67HttpFetchTask` object | 7208 | 2560-byte stack and `St67Runtime`, including its own 4096-byte `httpPayload`; `User/Src/WiFi/St67HttpFetchTask.cpp` |
| `LogService` object | 5504 | 1536-byte stack and a 16-entry queue of 201-byte events; `User/Inc/Debug/LogService.hpp` |
| `ConsoleService` object | 4000 | 2048-byte stack, 8-entry command queue of 128-byte lines, 256-byte RX ring; `User/Inc/Console/ConsoleService.hpp` |
| USB CDC buffers | 4096 | `UserRxBufferFS` and `UserTxBufferFS`, 2048 bytes each |
| `CurrentSenseTask` object | 2480 | 2048-byte stack |
| `MainLoopTask` object | 1960 | 1536-byte stack |
| `localBoard` (`PcbDisplayBoard`) | 1648 | 1024-byte `DisplayRefresh` stack, logical and prepared frames |
| `ClockTask` object | 1600 | 1024-byte stack |
| `led1` (`BlinkingLed`) | 1200 | 768-byte stack |
| FreeRTOS static support | about 3400 | Idle stack 512, timer stack 1024, their TCBs 384 each, ready lists 1120 |
| LwIP pools and tables | about 11000 | `memp_memory_*` pools 6770, DNS table 1184, IPv6 neighbour and destination caches 920, and smaller state |
| ST67 static driver state | about 1400 | Includes `W61_Obj` at 992 bytes |

By subsystem, summing the map's `.data` and `.bss` input sections: LwIP
44.6 KB (with its heap), FreeRTOS 44.0 KB (with its heap), `User/` code
34.2 KB, USB 6.9 KB, ST67 driver 1.4 KB, `Core/` 1.0 KB.

The same astro payload is held twice: once in `St67Runtime::httpPayload` as the
WiFi task receives it, and once in `AstroDataRefreshTask::responseBuffer_`,
the copy it hands over. See [Reduction Plan](#reduction-plan).

## FreeRTOS Usage

### Static reservation

- FreeRTOS dynamic heap: `40000` bytes
- Idle task stack: `512` bytes (`configMINIMAL_STACK_SIZE` 128 words)
- Timer task stack: `1024` bytes (`configTIMER_TASK_STACK_DEPTH` 256 words)
- Idle and timer TCBs: `384` bytes each
- Ready-task lists and scheduler state: about `1120` bytes
- The application task objects listed above, whose stacks are static

`configTOTAL_HEAP_SIZE` is a reservation for `heap_4.c`; it is not the same as
the heap currently allocated. The heap is shared by CMSIS-RTOS2 objects created
without static memory, ST67, LwIP and the generated `defaultTask`.

### Runtime diagnostics

`stats on` makes `LogService::emitStats()` report periodically:

```text
[STATS] sent=<lines> dropped=<lines> busyDrop=<lines>
[MEM] heapFree=<current free bytes> heapMin=<minimum-ever free bytes>
[STACK] name=<task> configured=<configured bytes> remaining=<high-water bytes>
```

`[STACK]` covers the application's `Task<>` objects only, not `defaultTask`
or the middleware tasks. `heapMin` measured during a worst-case ST67 connect,
DHCP and HTTP workload is the value to use when deciding whether
`configTOTAL_HEAP_SIZE` can be reduced. Every task stack should also be reduced
only after checking its high-water mark under its deepest call path.

### Tasks and configured stacks

Application tasks, with static stacks inside their objects:

| Task (name in `[STACK]`) | Stack | Priority | Source |
| --- | ---: | --- | --- |
| `AstroDataRefresh` | 3072 | Normal | `AstroDataRefreshTask.hpp`; 2048 overflowed once the clock sync ran on it, 760 left at the deepest path |
| `St67HttpFetch` | 2560 | BelowNormal | `User/Src/WiFi/St67HttpFetchTask.cpp` |
| `CurrentSense` | 2048 | BelowNormal | `CurrentSenseTask.hpp` |
| `ConsoleService` | 2048 | Normal | `ConsoleService.hpp` |
| `LogService` | 1536 | Normal | `LogService.hpp` |
| `MainLoopTask` | 1536 | Normal | `MainLoopTask.hpp` |
| `DisplayRefresh` | 1024 | Realtime | `PcbDisplayBoard.hpp` |
| `Clock` | 1024 | BelowNormal | `ClockTask.hpp` |
| `Led1` | 768 | Low | `BlinkingLed.hpp`, in `User/Src/AstroWeather.cpp` |

Generated and middleware tasks, with stacks from the FreeRTOS heap unless
noted:

| Task | Stack | Source |
| --- | ---: | --- |
| `defaultTask` | 512 | `Core/Src/main.c`, `128 * 4` |
| FreeRTOS idle | 512 | static, see above |
| FreeRTOS timer | 1024 | static, see above |
| LwIP `tcpip_thread` | 4096 | `TCPIP_THREAD_STACKSIZE` in `LWIP/Target/lwipopts.h` |
| ST67 netif task | 2048 | `NETIF_TASK_STACK` in `LWIP/App/lwip_netif.h` |
| ST67 modem RX task | 2048 | `W61_MDM_RX_TASK_STACK_SIZE_BYTES` in the ST67 driver |
| ST67 SPI transfer engine | 1536 | `SPI_THREAD_STACK_SIZE` in `ST67W6X_Network_Driver/Target/w61_driver_config.h` |

The LwIP and ST67 HTTP client tasks (`LWIP/App/http_client.c`,
`w6x_http.c`) are not linked: the firmware uses its own
`User/Src/WiFi/HttpClient.cpp` on the fetch task. `St67ProbeTask`
(`Task<2048>`) is compiled but has no instance in this image. The middleware
stack sizes are in bytes and converted to FreeRTOS words where the task is
created.

## LwIP Usage

LwIP reserves `33551` bytes for its heap. This comes from:

```c
#define PBUF_LINK_ENCAPSULATION_HLEN 388
#define FACTOR 64
#define MEM_MIN (2300 + FACTOR * (100 + PBUF_LINK_ENCAPSULATION_HLEN))
```

That calculates to `33532` bytes; with two block headers and alignment padding, `ram_heap` links at `33551`.

Additional linked LwIP pools and tables use about `11000` bytes. The current
configuration also enables or sizes several high-water features:

- TCP receive window: `22 * TCP_MSS`, approximately `32120` bytes of protocol window capacity
- TCP send buffer: `16 * TCP_MSS`, approximately `23360` bytes
- TCP segment and pbuf pools sized from the send buffer
- `TCPIP_MBOX_SIZE`: `64`
- Raw/UDP/TCP/accept mailbox sizes: `32`, `64`, `64`, and `32`
- IPv6, MLD, ND, IPv6 forwarding, and route-table support
- DHCP, DNS, IGMP, packet reassembly, and socket APIs

The TCP window and send-buffer values are protocol capacities; they are not
all directly reserved as one contiguous RAM allocation. They nevertheless drive
LwIP pool counts and can increase the amount of RAM required during traffic
bursts. The astro payload is about 1.5 KB, far below these capacities.

## ST67 Middleware Usage

ST67 has two kinds of RAM cost:

1. Static driver state linked into `.bss`, about `1400` bytes including the
   `992`-byte `W61_Obj`.
2. Dynamic allocations made from the shared FreeRTOS heap: the modem RX task
   and its receive buffer (`W61_MAX_SPI_XFER = 1520` bytes), the SPI transfer
   task, queues, event group and transfer buffers, the netif task, scan
   results, and network and socket state.

The dynamic allocations are not visible in the ELF because they come from
`ucHeap` at run time. Their effect is captured by `heapFree` and `heapMin`.

The ST67 source set also includes its complete W61 AT/Core implementation and
LwIP integration. Code inclusion mainly affects flash; only global/static
objects and runtime allocations affect RAM.

## Reduction Plan

Apply reductions in this order and validate each step with the complete
application workload.

### 1. Reduce the log queue: done

The queue, then in `DebugService`, held 64 events of about 200 bytes, some
12.8 KB. `LogService::kLogQueueDepth` is now 16, which saved about 9.6 KB. The
overflow policy drops the oldest event, so producers never block; burst
tolerance is lower, and `[STATS] dropped=` shows when that bites.

### 2. Measure before reducing the FreeRTOS heap: not done

Run the full ST67 workflow, including association, DHCP, HTTP, repeated
requests, and TLS if TLS is required. Record the lowest `heapMin` value. Reduce
`configTOTAL_HEAP_SIZE` only when that value leaves an explicit margin for
error paths and future changes.

Do not infer required heap from the current free value after initialization;
temporary HTTP, TLS, and scan allocations can produce a lower watermark later.

### 3. Hold the payload once: not done, new

The fetch task and the refresh task each keep a 4096-byte payload buffer.
Handing the refresh task's buffer to the fetch request, or parsing straight
from the fetch task's buffer while the refresh task still holds the request,
would save 4 KB without changing the maximum response size.

### 4. Reduce LwIP capacities: not done

The largest candidates are:

- `FACTOR` in `LWIP/Target/lwipopts.h`
- `TCP_WND`
- `TCP_SND_BUF`
- TCP segment/pbuf counts derived from `TCP_SND_BUF`
- TCP/IP and API mailbox depths

Reducing `FACTOR` from 64 to 48 would save roughly `7.8 KiB` of LwIP heap.
Reducing it to 32 would save roughly `15.6 KiB`. These changes must be tested
with the maximum expected packet size and concurrent traffic.

### 5. Tune task stacks from high-water marks: ongoing

Use the `[STACK]` diagnostics after exercising the deepest path. Reduce a stack
only when the observed margin remains comfortably above the largest interrupt,
library, formatting, and error-path requirements. `LogService::logf()` uses
formatted output and has already needed substantial stack headroom in callers:
`AstroDataRefreshTask` had to grow from 2048 to 3072 bytes.

### 6. Disable unused network features: not done

If the product does not need them, IPv6/MLD/ND, IGMP, packet reassembly, AP
support, unused socket features, and unused PPP sources can be removed or
disabled. Feature removal should be done through the applicable LwIP/ST67
configuration and regenerated project settings, not by deleting generated
source files ad hoc.

## Verification Commands

From Git Bash, with the toolchain on `PATH` (see
[Development.md](Development.md)):

```bash
arm-none-eabi-size -A -d build/<preset>/HostControllerA.elf
arm-none-eabi-nm -S --size-sort --radix=d -C build/<preset>/HostControllerA.elf
```

The most important link-time values are `.data`, `.bss`, `._user_heap_stack`,
`ucHeap`, and `ram_heap`. `arm-none-eabi-size -B` reports `bss` including
`._user_heap_stack`. Compare the link-time results again after every RAM
configuration change.

## Current Recommendation

About 13 KB remain. Before adding RAM-hungry features, first take the cheap
4 KB from holding the payload once, then measure `heapMin` under the worst-case
WiFi workload before touching the FreeRTOS heap, the LwIP heap or task stacks.
