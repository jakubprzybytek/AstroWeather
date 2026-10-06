> Archived 2026-10-06. Current state: [Firmware-RAM-Usage.md](../Firmware-RAM-Usage.md).

# RAM Usage History

Earlier measurements and the reduction plan, moved out of
[Firmware-RAM-Usage.md](../Firmware-RAM-Usage.md). Everything before
2026-10-03 is the T02 configuration, with LwIP on the host. In the T02 build
the largest reservations were the FreeRTOS heap (40 000 B), the LwIP heap
(`ram_heap`, 33 551 B) and LwIP's pools and tables (about 11 000 B); by
subsystem LwIP took 44.6 KB with its heap, FreeRTOS 44.0 KB with its heap,
`User/` code 34.2 KB, USB 6.9 KB, the ST67 driver 1.4 KB and `Core/` 1.0 KB.

## T01 switch, 2026-10-03

The ST67 driver moved from the T02 architecture (LwIP on the host) to T01
(TCP/IP in the module); see [WiFi.md](../WiFi.md). The Debug build linked on
2026-10-03 has `.data` 644 and `.bss` **90 264** bytes (Release: 456 and
90 260), against `.bss` 138 412 the day before: the LwIP heap (33 551), its
pools and tables (about 11 000) and the `tcpip_thread` and `netif` stacks are
gone, and the FreeRTOS heap no longer has to carry those two tasks. Text is
215 832 bytes in Debug and 110 152 in Release. The HTTP client now allocates
per request from the FreeRTOS heap: a 2 KiB header buffer, a 1 KiB read
buffer, a 512-byte request, plus the driver's own socket state. Run-time
`heapMin` under T01, measured on 2026-10-04 with the fetch task at 4096 B:
39 080 B free before the module starts, 29 384 B free with the module and
its two tasks up, **24 368 B minimum** through an HTTPS fetch, the same after
a second fetch in the same boot and across three boots. Under T02 the same
figure was 14 680 to 18 944 B.

**Heap reduced to 32 000 B, 2026-10-04.** With the peak demand measured at
15 632 B (15 888 B on a re-initialisation after `stop()`) and flat over 100
HTTPS cycles, `configTOTAL_HEAP_SIZE` went from 40 000 to 32 000 in the
`.ioc`. Debug `.bss` is now **83 808** B. Measured afterwards: 31 080 B free
before the module starts, 21 384 B with it up, `heapMin` **16 368 B** through
an HTTPS fetch, twice the 8 KiB floor. 100 cycles at this size: 99 passed,
one transient connect failure, `heapMin` 16 336 B, 30 808 B free after the
batch's `stop()`; details in the HTTPS plan's bench record.

The sections below are the T02 measurements and remain valid as the record of
that configuration; the per-subsystem figures will be re-measured once the T01
build has been on the bench.

## Summary, 2026-09-24 (T02)

Measured on 2026-09-24 from the Debug HostController build in
`build/Debug/` (`HostControllerA.elf` linked 2026-09-24 13:01; it
includes the `api` settings and low brightness), using `arm-none-eabi-size`
from GNU Tools for STM32 14.3.1:

| Item | Bytes |
| --- | ---: |
| RAM capacity, STM32G0B1CETx | 147456 (144 KiB): 142 KiB `RAM` region plus the 2 KiB `NOINIT` region of the error log |
| `.data` | 616 |
| `.bss` | 132792 |
| `._user_heap_stack`: C heap `0x200` + main stack `0x400` | 1536 |
| **Total statically reserved** | **134944** |
| **Remaining** | **12512** |
| Static RAM usage | about **91.5%** |

Since 2026-09-27 the top of RAM is a separate `NOINIT` region for the
error log, not cleared at startup so the log survives a reset; see
[Console.md](../Console.md#error-log). It was 2 KiB for 16 entries of 102-byte
text; since 2026-10-06 it is 5 KiB, `ErrorLog::Storage` being 4364 bytes for
24 entries of 160-byte text, and `RAM` 139 KiB. The link-time report shows
`RAM` and `NOINIT` separately. `errors` reads a copy of the log, a second
4364 bytes in `.bss` (`ErrorsCommand.cpp`). On 2026-10-06 the link reported
`RAM` 91216 bytes of 139 KiB (64.1%).

The previous measurement, on 2026-08-30 from `build/Debug/`,
was 131564 bytes of `.data + .bss` and 14352 bytes remaining. On 2026-09-23
it was 132632 with 13288 remaining; the 776 bytes since are mostly the settings
store's two 256-byte working images and the API host and path.

The percentage describes address-space reservation at link time; it does not
mean every byte is in use at every moment. About 73.5 KB of it is two heaps,
FreeRTOS and LwIP, whose use is only visible at run time.

## LwIP Usage (T02)

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

## Reduction Plan

Apply reductions in this order and validate each step with the complete
application workload.

### 1. Reduce the log queue: done

The queue, then in `DebugService`, held 64 events of about 200 bytes, some
12.8 KB. `LogService::kLogQueueDepth` is now 16, which saved about 9.6 KB. The
overflow policy drops the oldest event, so producers never block; burst
tolerance is lower, and `[STATS] dropped=` shows when that bites.

### 2. Measure before reducing the FreeRTOS heap: done 2026-10-04

Run the full ST67 workflow, including association, DHCP, HTTP, repeated
requests, and TLS if TLS is required. Record the lowest `heapMin` value. Reduce
`configTOTAL_HEAP_SIZE` only when that value leaves an explicit margin for
error paths and future changes.

Done under T01 with HTTPS: 100 persistent cycles, certificate failure cases
and a restart after `stop()` gave a peak demand of 15 888 B, and the heap was
reduced to 32 000 B (see the T01 section at the top). Saved 8 000 B of `.bss`.

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

## Current Recommendation

About 12.5 KB remain. Before adding RAM-hungry features, first take the cheap
4 KB from holding the payload once, then measure `heapMin` under the worst-case
WiFi workload before touching the FreeRTOS heap, the LwIP heap or task stacks.
