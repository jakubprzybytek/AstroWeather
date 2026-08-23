# ST67W611M1 Daily Fetch Feasibility and Implementation Plan

## 1. Purpose and scope

The target behavior is:

1. Wake or power up the ST67W611M1.
2. Join a configured Wi-Fi access point as a station.
3. Obtain network connectivity through LwIP running on the STM32 host.
4. Fetch data from a configured URL.
5. Close the network transaction and disconnect from the access point.
6. Put the ST67W611M1 into its lowest appropriate power state.
7. Process the fetched data on the host.
8. Repeat once per day.

The immediate implementation scope is only reliable communication with the ST67W611M1 through ST's `ST67W6X_Network_Driver`. HTTP, HTTPS, response parsing, daily scheduling, and host low power are later milestones.

## 2. Feasibility conclusion

**The functionality is feasible in this project, with required integration work and one important resource caveat.**

The project already has most of the correct software structure:

- X-CUBE-ST67W61 1.3.0 is vendored under `External/x-cube-st67w61`.
- The build selects `ST67_ARCH=W6X_ARCH_T02`, which is the required LwIP-on-host architecture.
- ST's service API, AT driver, SPI bus layer, netif bridge, LwIP 2.2.x, FreeRTOS, and board-specific `spi_port.c` are present in the build.
- The STM32G0B1 provides 512 KB flash and 144 KB RAM, above ST's stated minimum of 256 KB flash and 48 KB RAM.
- The board exposes SPI, `SPI_CS`, `SPI_RDY`, `CHIP_EN`, and `BOOT` signals.
- The existing `St67ProbeTask` has already exercised the physical SPI framing and basic AT exchange. This substantially reduces hardware uncertainty.

The current project is not yet able to use the official driver end to end:

- `Appli/App/main_app.h` and `Appli/App/app_config.h` are empty.
- No application code calls `W6X_RegisterAppCb`, `W6X_Init`, `W6X_WiFi_Init`, or `MX_LWIP_Init`.
- `St67ProbeTask` directly owns SPI, CS, RDY, and module power. It must not run concurrently with the ST driver.
- SPI1 is configured at only 62.5 kbit/s. This may prove control commands but is unsuitable for practical network traffic.
- SPI RX/TX DMA is not configured in the generated MSP code. The ST bus driver uses DMA for transfers above its threshold, so this is a functional blocker for full driver operation.
- The project uses a 40 KB FreeRTOS heap. ST reports roughly 13-40 KB dynamic use across examples, before this project's other tasks are considered. Runtime measurement and tuning are mandatory.
- Host LwIP consumes substantial static RAM. ST documents about 46 KB for its default LwIP projects; this project's `lwipopts.h` is somewhat reduced, but actual map and runtime measurements are still required.
- HTTPS is not currently complete. `http_client.c` is present, but mbedTLS is not linked and `MBEDTLS_CONFIG_FILE` is not defined. Plain HTTP can be added earlier; HTTPS requires a separate RAM/flash assessment.

### Feasibility rating

| Area | Assessment | Notes |
|---|---|---|
| Physical host-to-ST67 link | High confidence | Manual framed AT probe exists and has scan support. |
| Official driver bring-up | Feasible after fixes | Configure DMA/speed and replace direct probe ownership. |
| Station connect/disconnect | Feasible | Directly supported by W6X Wi-Fi APIs and T02 examples. |
| Host DHCP/DNS/TCP | Feasible | LwIP netif bridge and required LwIP sources are present. |
| Plain HTTP GET | Feasible | A host-side HTTP client is already generated. |
| HTTPS GET | Feasible but resource-sensitive | Requires mbedTLS, CA policy, entropy/time strategy, and memory validation. |
| ST67 daily shutdown/wake | Feasible | `W6X_DeInit()` powers off through `CHIP_EN`; reinitialization must be proven. |
| STM32 host daily sleep | Not assessed here | Separate RTC/wakeup and board power work is required later. |

## 3. Architecture required by T02

The selected T02 architecture places the responsibilities as follows:

```text
Application state machine
  |  Wi-Fi control: W6X_WiFi_*
  |  IP traffic: LwIP DNS/socket/HTTP APIs
  v
ST67W6X service API              Host LwIP
  | AT control                     | Ethernet-like frames
  +---------------+----------------+
                  v
        W61 AT driver + NET_IF
                  v
             spi_iface task
                  v
      SPI1 + DMA + CS/RDY/CHIP_EN
                  v
             ST67W611M1
       mission_t02 v2.0.106 firmware
```

T02 has two separate traffic types over the same SPI transport:

- AT commands and asynchronous events control system and Wi-Fi behavior.
- Network frames move directly between ST's `NET_IF` and host LwIP; they are not wrapped in AT commands.

The W6X network, HTTP, and MQTT offload APIs are disabled in T02. The application must use host LwIP for DHCP, DNS, sockets, HTTP, and future TLS.

## 4. Firmware and hardware prerequisites

### 4.1 ST67 firmware

Flash the ST67W611M1 with the T02 mission image that matches package 1.3.0. X-CUBE-ST67W61 1.3.0 corresponds to ST67 SDK firmware 2.0.106. `W6X_Init()` queries the module network mode and deliberately fails if a T01 image is used with a T02 host build.

Acceptance check:

- `W6X_Init()` reports network mode 0 and prints SDK version 2.0.106 or an explicitly validated compatible T02 version.

### 4.2 SPI and GPIO

Update the IOC and regenerate the STM32 peripheral code:

- Keep SPI1 full-duplex master, 8-bit data, and software-controlled CS.
- Configure SPI clock below ST's 40 MHz maximum. Start conservatively around 8 MHz, validate, then increase if useful.
- Add SPI1 RX and TX DMA channels with memory increment enabled and high priority.
- Enable the corresponding DMA interrupt and keep its priority valid for FreeRTOS API use.
- Keep `SPI_CS` as push-pull output with the polarity expected by `spi_port_set_cs()`.
- Keep `CHIP_EN` and `BOOT` as push-pull outputs; `BOOT` must be low for mission firmware.
- Keep `SPI_RDY` connected and readable. ST's package documentation recommends both-edge EXTI; the current driver polls RDY in its dedicated SPI task, but the generated configuration should remain aligned with the package unless inspection proves EXTI is unused for this target.
- Ensure SPI1 is not shared with another slave. ST specifies single-slave operation.

Electrical checks before driver bring-up:

- Confirm the module sees valid VDD before raising `CHIP_EN`.
- Guarantee at least 3.3 ms between module power becoming valid and `CHIP_EN` going high.
- Confirm the board provides the recommended 100 nF capacitor from `CHIP_EN` to ground.
- Confirm CS, RDY, BOOT, and CHIP_EN idle levels with a logic analyzer.

### 4.3 RTOS and memory

The driver requires FreeRTOS and creates at least a SPI transfer task and modem receive task. T02 also creates a netif task and LwIP TCP/IP task.

Before adding HTTP or TLS:

- Enable stack overflow checking, which is already configured.
- Record `xPortGetFreeHeapSize()` and `xPortGetMinimumEverFreeHeapSize()` after each milestone.
- Record stack high-water marks for the SPI, modem RX, netif, TCP/IP, debug, switch, and application tasks.
- Require at least 8 KB minimum-ever free FreeRTOS heap during extended connect/disconnect testing; increase this margin before TLS.
- Produce a linker map summary for flash, `.data`, and `.bss` after enabling the full T02 path.
- Reduce unused LwIP features and pools only from measured evidence. Do not reduce packet buffers until traffic tests pass.

## 5. Ownership and application boundaries

Create one application-level owner for the ST67 lifecycle. No other task may directly use SPI1, CS, RDY, or CHIP_EN while that owner is active.

Suggested C++ boundary:

```cpp
enum class St67State {
  Off,
  Starting,
  Ready,
  Connecting,
  Online,
  Disconnecting,
  Stopping,
  Fault
};

struct St67Result {
  bool success;
  int32_t driverStatus;
  uint32_t detail;
};
```

The owner should expose asynchronous commands or a single worker-task API rather than allowing arbitrary callers to invoke W6X APIs. W6X callbacks should only copy event data and set task/event flags; they should not run the full workflow.

During initial integration, replace `StartSt67ProbeTask()` in `AppVariant_Init()` with the official driver task. Keep the probe source available behind an explicit build option only if it remains useful for low-level diagnosis. Never compile both owners into an active runtime path.

## 6. Target state machine

```text
OFF
  -> STARTING: CHIP_EN/driver initialization
  -> READY: W6X and host LwIP initialized
  -> CONNECTING: station connect requested
  -> ONLINE: Wi-Fi connected and host DHCP has a nonzero address
  -> FETCHING: DNS and HTTP(S) transaction (later scope)
  -> DISCONNECTING: sockets closed, W6X_WiFi_Disconnect completed
  -> STOPPING: LwIP/netif and W6X teardown
  -> OFF: CHIP_EN low

Any active state
  -> FAULT: bounded timeout or driver error
  -> STOPPING: best-effort cleanup
  -> OFF: retry is scheduled with backoff
```

Wi-Fi association and usable IP connectivity are different conditions. `W6X_WIFI_EVT_CONNECTED_ID` means association succeeded; the fetch may begin only after the host LwIP station netif is link-up and DHCP has assigned a nonzero address. Do not use an unconditional fixed delay as the production DHCP gate.

## 7. Phased implementation

### Phase 0 - Establish reproducible baselines

1. Preserve logs from the current manual `AT` and `CWLAP` probes.
2. Record current build flash/RAM usage and FreeRTOS heap margin.
3. Record ST67 firmware identity and confirm that the installed image is T02 2.0.106.
4. Capture one successful SPI transaction on a logic analyzer: CS polarity, RDY transitions, clock mode, and startup timing.

Exit criteria:

- Hardware link evidence and baseline memory numbers are saved.
- T02 firmware compatibility is confirmed.

#### Phase 0.1 - Manual AT/CWLAP log baseline (2026-08-21)

The current manual SPI probe was run through the switch-triggered `AT` and
`CWLAP` paths. The module was not reset between probes: after the initial
startup exchange, subsequent probes reused the powered module and station-mode
state.

Representative successful startup and scan:

```text
[0:00:00:13] [INFO] ST67 startup: '\nready\n'
[0:00:00:13] [INFO] ST67 CWMODE=1: '\nOK\n'
[0:00:00:20] [INFO] ST67 CWLAP attempt 1/3: 21 read(s), terminated=1, 19 AP(s)
```

Additional completed scans in the same run reported:

| Scan result | SPI reads | Terminator | AP records |
|---|---:|---:|---:|
| Empty result | 2 | `OK` | 0 |
| Successful scan | 21 | `OK` | 19 |
| Successful scan | 17 | `OK` | 15 |
| Successful scan | 18 | `OK` | 16 |
| Successful scan | 16 | `OK` | 14 |

Representative AP records included:

```text
ST67 AP: +CWLAP:(3,"lemo",-32,"2c:fd:a1:39:86:d0",5,-1,-1,4,7,1)
ST67 AP: +CWLAP:(4,"Midgaard",-88,"52:e9:31:49:fa:4f",3,-1,-1,5,15,1)
ST67 AP: +CWLAP:(4,"PLAY_Swiatlowodowy_6B3C",-88,"5c:7b:5c:21:6b:...,3,-1,-1,5,15,1)
```

The SPI transport was stable during the completed scans. RDY assertion was
normally `0-2 ms` after the first startup, RDY deassertion was `0-1 ms`, and
the cold startup RDY assertion was approximately `570 ms`. The scan response
arrived over many framed SPI reads, with individual payloads typically around
`60-78` bytes. A complete result required up to 21 reads in this sample, so a
single-frame or fixed ten-read implementation is insufficient.

The same capture also contained completed zero-result scans and one
`CWMODE=1` response of `ERROR`. These observations are retained as diagnostic
evidence rather than treated as transport failures: the successful scans
returned the expected `OK` terminator and multiple `+CWLAP:` records, while
the cause of intermittent empty scans still requires correlation with RF
conditions, scan timing, and switch-trigger timing.

Baseline conclusion: raw SPI framing, CS/RDY handshaking, multi-frame AT
responses, and manual AP enumeration are demonstrated. The log capture does
not yet prove ST67 firmware identity, T02 compatibility, or complete RF scan
repeatability; those remain separate Phase 0 exit items.

#### Phase 0.2 - Resource baseline instrumentation (2026-08-21)

The baseline firmware now reports FreeRTOS resource watermarks through the
existing periodic USB CDC debug statistics path. `TaskBase` maintains a fixed,
non-allocating registry of constructed tasks, and `DebugService` emits:

```text
[MEM] heapFree=<bytes> heapMin=<bytes>
[STACK] name=<task> configured=<bytes> remaining=<bytes>
```

The stack value is converted from FreeRTOS stack words to bytes. Tasks that
have been constructed but not started are omitted from the stack report because
they do not have a valid runtime handle. The registry capacity is 16 tasks;
the current application is below this limit. The minimum-ever heap value is
not reset between reports, so the lowest value during a complete test session
is retained.

The first implementation build used the `Debug-HostController` preset with
the existing project configuration and completed successfully. The linker
reported:

| Region | Used | Capacity | Usage |
|---|---:|---:|---:|
| RAM | 72,368 B | 147,456 B | 49.08% |
| FLASH | 68,624 B | 524,288 B | 13.09% |

The section-level ELF report was:

| Section | Size | Role |
|---|---:|---|
| `.isr_vector` | 188 B | FLASH |
| `.text` | 65,944 B | FLASH |
| `.rodata` | 2,116 B | FLASH |
| `.data` | 352 B | SRAM, with FLASH load image |
| `.bss` | 70,476 B | SRAM |
| `._user_heap_stack` | 1,540 B | linker-reserved RAM |

The static measurement was produced from
`build/Debug-HostController/HostControllerA.elf` with
`arm-none-eabi-size -A`.

The image was flashed and exercised through USB CDC on the bench. The runtime
capture covered idle startup, `AT`, repeated `CWLAP` attempts, a successful
multi-frame scan, and a post-scan idle window. The observed heap values were
constant throughout the capture:

| Measurement | Value |
|---|---:|
| Configured FreeRTOS heap | 40,000 B |
| Lowest `heapFree` | 39,080 B |
| Lowest `heapMin` | 39,080 B |
| Maximum observed heap used | 920 B |

The per-task stack watermarks were:

| Task | Configured | Lowest remaining | Observation |
|---|---:|---:|---|
| `Led1` | 768 B | 600 B | Unchanged during probe |
| `SwitchTask` | 512 B | 316 B | Unchanged during probe |
| `DebugService` | 1,024 B | 120 B | Lowest overall margin during idle logging |
| `St67Probe` | 2,048 B | 504 B | Reduced from 1,852 B during SPI probing |

The runtime heap margin is comfortably above the Phase 0.2 acceptance target
of 8 KB: `39,080 B` remained free, or `31,080 B` above that target. The heap
minimum did not decline during the captured startup, AT exchange, scan retries,
successful scan, or idle period, so this run shows no evidence of a heap leak.
This is only a short run and does not replace the later repeated-cycle stress
test.

The stack result requires more caution. `St67Probe` retained `504 B` after the
largest observed scan, but `DebugService` retained only `120 B` during normal
periodic reporting. That is a narrow margin for future driver logging or longer
formatted messages. Before enabling the official ST67 driver, keep debug
logging bounded and treat the DebugService stack as a resource-risk item to
retest after each integration increment. No stack-overflow hook fired in this
capture.

The ST67 transport completed startup and returned `OK` for `CWMODE=1` after an
initial blank response. The first `CWLAP` invocation returned zero APs on all
three attempts. A later invocation returned zero APs on attempt 1, then found
18 APs on attempt 2 using 20 SPI reads and a valid terminator. This confirms
that the response path handles a large multi-frame scan, but also confirms that
empty scans remain intermittent and need RF/timing correlation. The log shows
`busyDrop=269` before useful output was established, then `sent=9` and later
`sent=64`; the initial CDC line was visibly truncated/interleaved. These are
debug transport capture-quality issues, not evidence of an ST67 SPI failure,
but future baseline logs should use a capture setup that preserves complete
lines.

Phase 0.2 runtime acceptance is complete for this bench run: static FLASH/RAM
usage is recorded, runtime heap and stack watermarks are recorded before and
after manual ST67 activity, a successful multi-frame scan is present, and no
heap decline or stack overflow was observed. T02 firmware identity and
long-duration/repeated-cycle resource stability remain separate Phase 0 exit
items. No heap, LwIP, task-size, SPI, or DMA tuning was made for this baseline.

### Phase 1 - Make the generated transport production-ready

1. Update `HostControllerA.ioc` for an approximately 1 MHz SPI clock and SPI1 RX/TX DMA. The initial production-ready target is intentionally conservative; increase the clock only after the transport is stable on the bench.
2. Regenerate and review only the expected changes in `Core/Src/main.c`, `Core/Src/stm32g0xx_hal_msp.c`, interrupt files, and DMA handles.
3. Verify that all SPI DMA completion callbacks reach `spi_port_transaction_complete_cb`, and that ST67 RDY rising edges reach `spi_on_txn_data_ready`.
4. Change SPI error handling from a permanent global halt to an error path that the lifecycle owner can report and recover from, after basic bring-up is proven.
5. Validate DMA completion and repeated raw port initialization/deinitialization with a temporary bounded low-level harness. Keep `W6X_Init()` and end-to-end W61 protocol validation in Phase 2.

#### Phase 1 validation result (2026-08-21)

The temporary DMA validation task was run for 100 init/deinit cycles and then
removed from the product source. The captured result was:

```text
cycles=100 starts=100 completions=100 timeouts=0 halErrors=0 initFailures=0
heapFree=39080 heapMin=39080
```

All 100 DMA transfers completed, with no timeout, HAL error, or initialization
failure. FreeRTOS heap usage remained stable at 39,080 bytes free. The first
run used a 1,024-byte validation stack and reached only 168 bytes remaining
after the summary log. The stack was increased to 1,536 bytes for the final
run; it reported 680 bytes remaining after the test and 1,224 bytes before
the summary log. No stack overflow occurred. The `heapBefore=3908` field in
the captured summary was truncated by the fixed 127-character debug record;
the `[MEM]` lines confirm the full value was 39,080.

The validation task and its compile-time switch were removed after this
measurement. The normal HostController startup now returns to `St67ProbeTask`.
The result validates the MCU DMA completion path and repeated raw port
cycling, but does not validate a complete W61 protocol transaction. That
remains a Phase 2 `W6X_Init()` smoke-test responsibility.

Exit criteria:

- SPI1 measures approximately 1 MHz with the expected mode-0 timing.
- DMA-backed SPI transfers completed 100/100 without timeout or HAL error in the controlled low-level test.
- The transport recovers from an aborted/error transfer without entering `Error_Handler()` or leaving CS asserted.
- CHIP_EN and port callback state remained correct over 100 init/deinit cycles.
- End-to-end W61 protocol and package RDY/CS conformance are explicitly deferred to the Phase 2 `W6X_Init()` smoke test.

### Phase 2 - Official W6X driver smoke test

Detailed implementation steps, callback ownership, failure boundaries, and
validation gates are defined in
[`ST67_Phase_2_Implementation_Plan.md`](ST67_Phase_2_Implementation_Plan.md).

Implement the smallest official-driver task, following the package's T02 examples:

1. Create a persistent `W6X_App_Cb_t` callback table. Its lifetime must exceed the initialized driver lifetime.
2. Call `W6X_Init()` as the first W6X API and verify module/network-mode information.
3. Call `W6X_RegisterAppCb()` before Wi-Fi initialization.
4. Call `W6X_WiFi_Init()`.
5. Call `MX_LWIP_Init()` only after Wi-Fi initialization, because it queries station/AP MAC addresses and initializes `W6X_Netif`.
6. Run `W6X_WiFi_Scan()` and wait on a bounded event flag for scan completion.
7. Log structured status and memory watermarks through `DebugService`.
8. On shutdown, deinitialize in reverse order once the necessary deinit behavior has been verified.

Do not initialize BLE, soft AP, MQTT, shell, or ST's logging task for this product path. They consume RAM and are not needed for the daily station workflow.

Exit criteria:

- Boot, module information, Wi-Fi init, LwIP init, and scan all succeed from a cold `CHIP_EN`-low start.
- The sequence succeeds repeatedly without leaked heap.

#### Phase 2 validation result (2026-08-21)

The first bench run completed the official-driver smoke test after a switch 1
press. The module reported X-CUBE-ST67W61 1.3.0 and SDK 2.0.106;
`W6X_Init()`, callback registration, `W6X_WiFi_Init()`, and `MX_LWIP_Init()`
all returned success. The asynchronous scan callback completed successfully
and reported 20 APs.

Resource measurements from the run were:

| Point | Free heap | Minimum-ever heap |
|---|---:|---:|
| Before W6X | 39,080 B | 39,080 B |
| After W6X | 33,184 B | 31,536 B |
| After Wi-Fi | 33,104 B | 31,464 B |
| After LwIP | 24,752 B | 24,752 B |
| After scan | 23,824 B | 22,136 B |

The ST67 service task retained 472 bytes at its lowest observed watermark and
the run had no debug queue drops, USB overflow, or truncation. DebugService
initially retained only 128 bytes, below the Phase 2 256-byte margin, so its
static stack was increased from 1,024 to 1,536 bytes. The scan request was also
reduced from 32 to 20 results to match the vendor's fixed
`W61_WIFI_MAX_DETECTED_AP=20` storage and eliminate the corresponding warning.

The run is functionally successful. A post-stack-change watermark capture and
ten cold-boot repetitions remain required before closing the Phase 2
definition of done.

#### Phase 2 post-fix validation (2026-08-21)

After increasing the official driver's SPI transfer task stack from 768 to
1,536 bytes, the scan completed without further HardFaults. Two post-scan
captures reported stable heap values:

| Capture | Free heap | Minimum-ever heap |
|---|---:|---:|
| 1 | 23,056 B | 21,368 B |
| 2 | 23,056 B | 21,376 B |

The result supports the stack-corruption diagnosis and confirms that the
official-driver scan path remains within the Phase 2 8 KB minimum heap margin.
The ten cold-boot repetition test is still outstanding.

### Phase 3 - Connect, DHCP, disconnect, and shutdown

Detailed lifecycle steps, teardown requirements, failure behavior, and
validation gates are defined in
[`ST67_Phase_3_Implementation_Plan.md`](ST67_Phase_3_Implementation_Plan.md).
The remaining work after the successful single-cycle bench result is ordered
in
[`ST67_Phase_3_Implementation_Plan.md`](ST67_Phase_3_Implementation_Plan.md).

1. Store SSID/password in a product configuration boundary, not directly in generated files or source control. For initial bench testing, a local ignored configuration header is acceptable.
2. Populate `W6X_WiFi_Connect_Opts_t` and call `W6X_WiFi_Connect()`.
3. Handle connected, disconnected, reason, and driver error callbacks.
4. Wait for LwIP station netif link-up and a nonzero DHCP address with a bounded timeout.
5. Log SSID, channel, RSSI, IPv4 address, gateway, DNS server, elapsed association time, and elapsed DHCP time. Do not log credentials.
6. Optionally resolve a test hostname through LwIP DNS to prove the complete T02 data path without implementing HTTP.
7. Call `W6X_WiFi_Disconnect(1)` and wait for the disconnected event. The `restore=1` choice prevents automatic reconnection from undermining the daily duty cycle.
8. Shut down the module with `W6X_DeInit()`. In package 1.3.0 this sets hibernate, deinitializes W61, and drives `CHIP_EN` low through `spi_port_deinit()`.
9. Re-run the full initialization path after a delay to prove that driver and netif resources can be recreated safely.

The T02 LwIP code currently has no public deinitializer and allocates netif structures/tasks dynamically. This must be resolved before daily `W6X_DeInit()`/reinit is accepted. Choose one of these designs after a focused test:

- Preferred if supported: initialize W6X/LwIP once, disconnect daily, leave host network tasks alive, and use ST67 automatic standby between jobs. This avoids task/netif recreation but does not reach the 200 nA shutdown state.
- Preferred for minimum module energy: add a symmetric, idempotent LwIP/netif teardown, then call `W6X_DeInit()` and use `CHIP_EN` shutdown. This needs careful task, timer, netif, DHCP, and allocation cleanup.
- If the host also reboots or enters a reset-equivalent state each day: cold boot with `CHIP_EN` low and initialize everything once per host boot. This is operationally simple and compatible with full ST67 shutdown.

Exit criteria:

- 100 consecutive connect, DHCP, disconnect cycles pass.
- At least 20 shutdown and cold reinitialization cycles pass if full shutdown is selected.
- No downward trend appears in minimum-ever heap or task count.

#### Phase 3 bench validation result (2026-08-22 to 2026-08-23)

One manual lifecycle completed successfully using the local ignored
credentials. The ST67 reported middleware 1.3.0 and SDK 2.0.106, associated
with SSID `lemo` on channel 5 at RSSI `-35`, and host LwIP assigned
`192.168.1.142`. The reported netmask was `255.255.255.0`, gateway and DNS
were both `192.168.1.1`, and the station link was brought down cleanly during
teardown.

The run completed without transport drops, RX overflow, or RX truncation.
FreeRTOS resource measurements were 39,080 B initial free heap, 21,896 B
minimum-ever free heap, and 33,720 B free heap after teardown. The ST67
service retained 720 B from its 2,560 B stack and DebugService retained 632 B
from its 1,536 B stack. This satisfies the single-cycle functional, heap, and
stack checks.

Subsequent three-cycle full-shutdown validation kept the instrumented LwIP,
Wi-Fi, SPI, command-handler, and modem allocation counters balanced, with
zero outstanding pbufs, a stopped netif worker, eight tasks, and final
`CHIP_EN=0`/`RDY=0`. The command-handler mutex cleanup removed the large
recurring leak seen before that fix. An earlier repeated full-shutdown run
still showed an unresolved approximately 64-byte-per-cycle free-heap decline,
so full-shutdown stability is not yet accepted as proven.

The explicit compile-time lifecycle mode is now implemented: persistent mode
initializes W6X, Wi-Fi, and host LwIP once, repeats connect/DHCP/disconnect,
and performs one final teardown after the batch. The 100 persistent cycles, 20 cold
shutdown/reinitialization cycles, and electrical verification of final
`CHIP_EN` and `SPI_RDY` levels remain outstanding. Persistent initialization
and automatic standby is the current operational fallback if full shutdown
cannot meet the later power and stability gates.

A 20-cycle cold-restart smoke capture subsequently reported `20/20` passed
cycles with zero pbufs, removed netifs, a stopped worker, and final
`CHIP_EN=0`/`RDY=0`. Debug transport drops, RX overflow, and RX truncation
were zero. The run measured 39,080 B batch-start free heap, 32,680 B batch-end
free heap, and 20,664 B minimum-ever free heap. Task count changed from 7 to 8,
so the resource gate remains open; the required persistent batch and formal
cold-restart acceptance are still outstanding. The source default is
`SingleFullShutdown`.

The subsequent three-cycle `PersistentStress` run completed `3/3` cycles.
W6X, Wi-Fi, and host LwIP initialized once; cycles 2 and 3 retained the live
netifs and worker, and one final teardown removed them. Each cycle passed
DHCP, disconnect callback, link-down, and IPv4-clearing checks. The batch
ended with zero pbufs, removed netifs, a stopped worker, and
`CHIP_EN=0`/`RDY=0`; debug transport drops, RX overflow, and RX truncation
were zero. Free heap stabilized at 23,984 B after each cycle, with 21,856 B
minimum-ever. The service and DebugService stack margins were 848 B and
640 B. The persistent count is now `100U` for the planned R4 run; the source
default remains `SingleFullShutdown`.

The planned 100-cycle `PersistentStress` run subsequently completed `100/100`
cycles. W6X, Wi-Fi, and host LwIP initialized once; all cycles passed DHCP,
disconnect callback, link-down, and IPv4-clearing checks, and one final
teardown removed the netifs and stopped the worker. Free heap stabilized at
23,984 B after each cycle, with 21,552 B minimum-ever. The service and
DebugService stack margins were at least 872 B and 656 B, and the active task
count remained 11. Debug transport drops, RX overflow, and RX truncation were
zero. R4 persistent acceptance passes; cold-restart resource stability,
failure-path checks, electrical verification, and the R2 heap gate remain
open.

An incorrect-password single-cycle run produced a bounded `connect` failure
with status `2 (ERROR)` and preserved `connect/2` as the primary result.
Cleanup completed with zero pbufs, removed netifs, a stopped worker, and
`CHIP_EN=0`/`RDY=0`; debug transport drops, RX overflow, and RX truncation
were zero. Free heap was 33,896 B after cleanup with 22,328 B minimum-ever.
The remaining bounded failure-path checks are still open.

A missing-credentials single-cycle run failed before W6X initialization with
`credentials-unavailable` and status `0`. Heap remained 39,080 B with no
resource or task allocation, and the aggregate result preserved the original
failure stage. The remaining bounded failure-path checks are still open.

An AP-unavailable single-cycle run produced a bounded `connect` failure with
status `2 (ERROR)` after approximately 3.6 seconds, followed by successful
cleanup and recovery on a later retry after the AP became available. The
cleanup result was zero pbufs, removed netifs, a stopped worker, and
`CHIP_EN=0`/`RDY=0`. No RX overflow or truncation occurred; five USB
`busyDrop` events were present before the lifecycle and did not increase
during it. The AP-unavailable lifecycle case passes functionally, while the
remaining bounded failure-path checks and clean transport baseline remain
open.

The remaining failure-path checks are intentionally deferred for now because
they require difficult bench conditions. They remain open before formal Phase
3 closure; the normal lifecycle implementation and persistent stress result
remain valid.

A repeated 20-cycle cold-restart run completed `20/20` cycles functionally,
with zero pbufs, removed netifs, a stopped worker, and final
`CHIP_EN=0`/`RDY=0`. Post-cycle free heap declined from 33,896 B after cycle 1
to 27,864 B after cycle 20, with 15,744 B minimum-ever, confirming an
unresolved full-shutdown resource-loss trend. Task count returned to 8 after
each teardown. Eleven pre-existing USB `busyDrop` events did not increase;
RX overflow and truncation were zero. Full-shutdown resource stability remains
open, so automatic standby remains the safer fallback until the leak source
is identified.

### Phase 4 - Host network fetch, intentionally deferred

Start with a deterministic small HTTP endpoint on the local network:

1. Resolve the hostname with LwIP DNS.
2. Use the generated host-side `HTTP_Client_Request()` or a minimal LwIP socket client.
3. Stream response chunks into a bounded consumer; do not allocate based solely on remote `Content-Length`.
4. Enforce DNS, connect, receive, total transaction, and maximum-response limits.
5. Validate status code, content type, framing, and truncation handling.
6. Close the socket before Wi-Fi disconnect.

For production HTTPS:

1. Add mbedTLS 3.6.x compatible with the package and define `MBEDTLS_CONFIG_FILE`.
2. Select only required TLS algorithms and certificate parsing features.
3. Define trust policy: pinned CA or public CA bundle, hostname verification mandatory.
4. Provide a cryptographically appropriate entropy source.
5. Provide certificate-time validation. SNTP requires network connectivity and itself needs a trust/bootstrap policy; a retained RTC or constrained certificate strategy may be preferable.
6. Measure worst-case TLS heap and stack use on the STM32G0B1 before accepting HTTPS as feasible in the final image.

Exit criteria:

- The complete response is fetched repeatedly with bounded memory.
- Failure at every network stage still proceeds to disconnect and the selected power state.

### Phase 5 - Daily scheduler and data handoff, later scope

1. Trigger the lifecycle owner from an RTC/alarm scheduler rather than a 24-hour RTOS delay.
2. Use a monotonic job identifier and persist last-success time if missed or duplicate fetches matter.
3. Apply bounded retry with exponential backoff inside a daily attempt window.
4. Hand an immutable, length-delimited response to the processing task through a queue or ownership-transfer buffer.
5. Keep scheduling, transport, and parsing as separate components so malformed data cannot strand the radio online.

## 8. Power-mode decision

ST documents two relevant mechanisms:

- `W6X_SetPowerMode(1)` enables NCP power save. When unconnected, the module enters standby after commands. While connected, it uses DTIM standby by default. CS is also the hardware wake signal from deep sleep.
- Driving `CHIP_EN` low holds the module in reset and enters shutdown, with a documented typical consumption of 200 nA. Raising it starts a cold module boot.

For a once-per-day transaction, shutdown is the likely final choice, but only after lifecycle teardown/reinitialization is leak-free. Until then, automatic standby is the safer integration state.

The current `W6X_POWER_SAVE_AUTO=1` is appropriate. ST warns that the NCP must exit low power before data traffic; the package's SPI/CS and Wi-Fi API implementation handles this. Application code should use W6X APIs rather than manipulating CS to wake the module.

## 9. Error handling and observability

Every stage needs a bounded timeout and a stable result code:

| Stage | Required diagnostics |
|---|---|
| Driver init | W6X status, module SDK, T01/T02 mismatch, elapsed time |
| Scan | completion/timeout, result count |
| Association | W6X reason event, elapsed time, retry count |
| DHCP | link state, address, timeout, elapsed time |
| DNS | hostname, LwIP error, elapsed time |
| Fetch | status, bytes, HTTP status, timeout stage |
| Disconnect | event/reason, elapsed time |
| Shutdown | CHIP_EN/RDY final levels, elapsed time |
| Resources | free/minimum heap, task stack high-water marks |

Never log Wi-Fi passwords, authorization headers, URL secrets, response secrets, or certificate private material.

Recovery policy:

1. Close any open socket.
2. Request Wi-Fi disconnect if the driver is responsive.
3. Stop or deinitialize networking according to the selected lifecycle design.
4. Drive the module to a known power state.
5. Report the failure and schedule a bounded retry.
6. Escalate to host reset only after repeated transport-level failures and only if other product functions permit it.

## 10. Validation matrix

### Bench functional tests

- Cold boot with access point available and unavailable.
- Correct, incorrect, and changed credentials.
- AP disappears during association, DHCP, DNS, and transfer.
- DHCP server absent or slow.
- DNS failure and server refusal.
- Repeated manual button-triggered lifecycle before daily scheduling exists.
- Disconnect while traffic is idle and active.
- ST67 shutdown followed by cold wake and reconnect.

### Stress tests

- 100 connect/disconnect cycles.
- 20 or more full `CHIP_EN` shutdown/reinitialization cycles.
- 24-hour run with frequent accelerated jobs.
- Maximum expected response and deliberately oversized response.
- USB debug traffic concurrent with Wi-Fi traffic.
- Heap and task-stack watermark collection at every iteration.

### Electrical and power tests

- Logic-analyzer capture of SPI/DMA and CS/RDY timing at the selected clock.
- Measure ST67 current in off, disconnected standby, associated DTIM standby, and active transfer states.
- Confirm `CHIP_EN` low reaches shutdown current and RDY reaches the expected idle level.
- Confirm no host GPIO back-powers the module while `CHIP_EN` is low.

## 11. Expected file changes by milestone

| File or area | Planned change |
|---|---|
| `HostControllerA.ioc` | SPI speed, RX/TX DMA, interrupt/platform settings |
| `Core/Src/main.c` and MSP/IRQ files | Regenerated SPI/DMA setup only |
| `ST67W6X_Network_Driver/Target/spi_port.c` | Minimal board/error/recovery adaptations if required |
| `Appli/App/app_config.h` | Non-secret defaults, timeouts, power policy; no committed credentials |
| `User/Inc/HostController` | Lifecycle owner public API and result types |
| `User/Src/HostController` | Driver state machine and callbacks |
| `User/Src/HostController/AppVariant.cpp` | Start official lifecycle owner instead of manual probe |
| `User/Src/HostController/St67ProbeTask.cpp` | Disable from normal runtime or remove after migration |
| `LWIP/App/lwip.c` and `lwip_netif.c` | Event hooks and, if full shutdown is selected, symmetric teardown |
| `LWIP/Target/lwipopts.h` | Only measured memory/feature tuning |
| `Core/Inc/FreeRTOSConfig.h` | Heap adjustment based on measured peak use |
| CMake files | Remove unused modules; later add mbedTLS only for HTTPS |

Generated CubeMX files should be changed through the IOC where possible. User code should remain outside generated files except for small, protected port hooks.

## 12. Recommended first implementation increment

The first coding increment should do exactly this:

1. Configure SPI1 DMA and a practical SPI clock.
2. Disable the manual probe runtime path.
3. Add one worker task with a persistent callback table.
4. Execute `W6X_Init -> W6X_WiFi_Init -> MX_LWIP_Init -> scan` once on button press.
5. Log status and heap/stack watermarks.
6. Leave the ST67 initialized after the scan; do not yet add connect, HTTP, or repeated teardown.

This increment isolates the highest-risk transition: moving from proven raw SPI framing to ST's multi-task T02 transport and host netif.

## 13. References reviewed

### ST documentation

- Introduction to Wi-Fi, X-CUBE-ST67W61 section: https://wiki.st.com/stm32mcu/wiki/Connectivity:Introduction_to_Wi-Fi#X-CUBE-ST67W61
- X-CUBE-ST67W61 Overview: https://wiki.st.com/stm32mcu/wiki/Connectivity:X-CUBE-ST67W61_Overview
- X-CUBE-ST67W61 Architecture: https://wiki.st.com/stm32mcu/wiki/Connectivity:X-CUBE-ST67W61_Architecture
- ST67W6X Echo Application: https://wiki.st.com/stm32mcu/wiki/Connectivity:Wi-Fi_ST67W6X_Echo_Application
- How to use X-CUBE-ST67W61 STM32CubeMX pack: https://wiki.st.com/stm32mcu/wiki/Connectivity:Wi-Fi_How_to_use_X_CUBE_ST67W61_STM32CubeMx_pack
- ST67W611M1 CHIP_EN and shutdown feature: https://wiki.st.com/stm32mcu/wiki/Connectivity:ST67W611M1_shutdown
- ST67W611M1 wake-up feature: https://wiki.st.com/stm32mcu/wiki/Connectivity:ST67W611M1_Wake-up
- ST67W611M1 32.768 kHz and low-power operation: https://wiki.st.com/stm32mcu/wiki/Connectivity:ST67W611M1_32KHz_management

### Local sources

- Local X-CUBE-ST67W61 1.3.0 release notes and T02 CLI, MQTT, and FOTA examples
- Current project CMake, IOC, SPI port, LwIP/netif, W6X configuration, and manual probe implementation

Documentation and package content were assessed against their state on 2026-08-21.