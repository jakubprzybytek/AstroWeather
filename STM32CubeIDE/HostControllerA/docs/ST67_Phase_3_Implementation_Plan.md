# ST67W611M1 Phase 3 Connect and Shutdown Implementation Plan

## 1. Objective

Extend the Phase 2 official-driver service into a bounded station lifecycle:

```text
trigger
  -> initialize W6X, Wi-Fi, and host LwIP when required
  -> connect to the configured access point
  -> wait for host LwIP DHCP
  -> record connection and network diagnostics
  -> disconnect and clear stored credentials
  -> tear down the ST67 netif
  -> shut down the module
  -> prove cold reinitialization
```

HTTP, HTTPS, response parsing, and daily scheduling remain outside this phase.
 
The single-cycle path has been implemented and validated. The ordered work
needed to close the remaining stress, teardown, failure, and electrical gates
is defined in
[`ST67_Phase_3_Remaining_Work_Plan.md`](ST67_Phase_3_Remaining_Work_Plan.md).

## 2. Preconditions and source-backed constraints

1. Complete the outstanding Phase 2 ten-cold-boot validation before changing
   the lifecycle path.
2. Keep `St67ServiceTask` as the only application owner of the ST67 transport
   and lifecycle.
3. In the selected T02 architecture, DHCP runs on host LwIP.
   `W6X_WIFI_EVT_GOT_IP_ID` is compiled only for T01 and must not be used as
   the T02 connectivity gate.
4. `W6X_WiFi_Connect()` and `W6X_WiFi_Disconnect()` contain bounded internal
   waits. The application must still bound its separate DHCP and cleanup
   stages.
5. `W6X_WiFi_Disconnect(1)` removes the stored connection information and
   prevents autoconnect from defeating the daily duty cycle.
6. The generated `MX_LWIP_Init()` is currently one-way: it allocates two
   netifs, starts the TCP/IP core, and creates the ST netif task without a
   public symmetric teardown. Do not call it repeatedly until teardown and
   partial-failure rollback are implemented and validated.
7. Keep the LwIP TCP/IP core alive across cycles unless a supported,
   independently validated shutdown mechanism is found. Recreate only the
   ST67-facing netifs and worker resources.

## 3. Configuration and credential boundary

Add only non-secret lifecycle settings to `Appli/App/app_config.h`:

- DHCP timeout;
- disconnect timeout;
- shutdown settling delay;
- cold-restart delay;
- optional accelerated-cycle count for bench testing.

Provide a tracked credential template containing no usable SSID or password
and an ignored local configuration header for bench credentials. The normal
tracked build must still compile when that local header is absent and report a
stable `credentials-unavailable` result without calling
`W6X_WiFi_Connect()`.

Before copying credentials into `W6X_WiFi_Connect_Opts_t`:

1. zero-initialize the complete structure;
2. reject an empty SSID;
3. reject strings longer than `W6X_WIFI_MAX_SSID_SIZE` or
   `W6X_WIFI_MAX_PASSWORD_SIZE`;
4. guarantee null termination;
5. set a finite reconnection policy rather than the driver's zero-value
   unlimited retry behavior;
6. leave WPS, WEP, and secured-credential mode disabled unless explicitly
   required by a later product decision.

Never log the password or include credentials in tracked source, build
artifacts, test records, or crash diagnostics.

## 4. Application state and callbacks

Extend the service lifecycle to:

```text
Off
Starting
Ready
Connecting
Online
Disconnecting
Stopping
Complete
Fault
```

Add task flags for connected, disconnected, connection reason, DHCP-ready,
and driver error. Clear all operation-specific flags before each stage.

The Wi-Fi callback must:

- set the connected flag for `W6X_WIFI_EVT_CONNECTED_ID`;
- set the disconnected flag for `W6X_WIFI_EVT_DISCONNECTED_ID`;
- copy the scalar reason code immediately for `W6X_WIFI_EVT_REASON_ID`;
- record connecting as diagnostic state only;
- ignore `W6X_WIFI_EVT_GOT_IP_ID` as a T02 readiness mechanism;
- return without delays, lifecycle calls, or retaining callback pointers.

The driver-error callback continues to copy its status and wake the worker.
Only the worker task may advance the lifecycle or perform cleanup.

Stable failure stages must include:

```text
credentials-unavailable
credentials-invalid
connect
connect-state
dhcp
disconnect
link-down
netif-stop
netif-remove
wifi-deinit
w6x-deinit
restart
```

Preserve the first failure as the final result while separately logging any
best-effort cleanup failures.

## 5. Implementation sequence

### Increment 3.1 - Compile the lifecycle boundary

1. Add the credential template and ignore rule.
2. Add non-secret Phase 3 timeouts and cycle settings.
3. Extend `St67ServiceTask` state, flags, event storage, and public trigger
   naming for a connectivity cycle.
4. Keep switch 1 as the manual trigger and reject a trigger while a cycle is
   active.
5. Compile both firmware variants when shared headers are changed.

Acceptance check: both variants build without local credentials, missing
credentials fail before connection, and no secret is present in tracked files.

### Increment 3.2 - Associate with the access point

1. Start from the Phase 2 `Ready` state after W6X, Wi-Fi, and LwIP
   initialization.
2. Validate and copy local credentials into a zero-initialized
   `W6X_WiFi_Connect_Opts_t`.
3. Clear stale callback and driver-error flags and set state to `Connecting`.
4. Call `W6X_WiFi_Connect()` once.
5. Treat its return as the authoritative bounded association result; use the
   application callback flags and copied reason code for diagnostics.
6. Query `W6X_WiFi_Station_GetState()` after success and require the connected
   state.
7. Copy and log bounded connection information: SSID, channel, RSSI, security,
   protocol, and association elapsed time.

Acceptance check: correct credentials associate; incorrect credentials,
missing AP, timeout, and reason events produce distinct bounded results.

### Increment 3.3 - Gate online state on host DHCP

Add a narrow application-facing LwIP status interface. Prefer a status
callback/event notification over polling, but make the final worker check
authoritative.

The worker may enter `Online` only when:

1. `netif_get_interface(NETIF_STA)` is non-null;
2. the station netif is up and link-up;
3. its IPv4 address is nonzero;
4. all conditions occur before the configured DHCP timeout.

Capture a synchronized snapshot containing IPv4 address, netmask, gateway, and
the first configured LwIP DNS server. Do not use an unconditional delay or a
W6X got-IP event.

On timeout, record link state, DHCP state, and elapsed time, then proceed to
best-effort disconnect. Optionally add a separately configurable host-LwIP DNS
lookup after DHCP; DNS resolution is diagnostic and not a core Phase 3 exit
criterion.

Acceptance check: normal and delayed DHCP succeed, absent DHCP times out
without hanging, and all reported network values come from host LwIP.

### Increment 3.4 - Prove persistent connect/disconnect cycling

Before implementing shutdown:

1. set state to `Disconnecting`;
2. call `W6X_WiFi_Disconnect(1)`;
3. require a successful return and observed disconnected event;
4. require the station netif link to become down and its IPv4 address to be
   cleared;
5. return to the persistent initialized `Ready` state;
6. permit another manual cycle without recreating W6X or LwIP resources.

For failures after association, disconnect is best-effort. Run 100 consecutive
connect, DHCP, and disconnect cycles while recording heap, task count, stack
watermarks, and stage timings.

Acceptance check: all cycles pass without automatic reconnect, task growth,
heap loss, stale DHCP state, callback leakage, or credential logging.

### Increment 3.5 - Add symmetric ST67 netif teardown

Separate one-time LwIP core initialization from repeatable ST67 interface
creation. Add idempotent teardown APIs to `lwip.c/.h` and
`lwip_netif.c/.h`.

Teardown must execute in a safe order:

1. prevent new ST67 RX notifications from reaching application-owned
   resources;
2. release and stop station DHCP and delete its timer;
3. stop soft-AP DHCP resources if they were ever allocated;
4. mark station and AP links and interfaces down;
5. stop and delete the ST netif worker task;
6. drain or account for outstanding custom pbufs before driver shutdown;
7. call `W6X_Netif_DeInit()` to clear link callbacks;
8. remove both netifs through thread-safe LwIP APIs;
9. free both allocated netif structures and any AP table;
10. clear every pointer, timer, task handle, and cached address so a second
    teardown is harmless.

Add rollback for each partial `MX_LWIP_Init()` failure. Confirm whether the
vendor SPI network bindings require an explicit unbind; do not accept
recreation while an old callback or queue binding can survive.

Acceptance check: repeated create/destroy tests restore free heap and task
count and never execute a callback against removed resources.

### Increment 3.6 - Shut down and cold-reinitialize the module

After a successful disconnect and netif teardown:

1. set state to `Stopping`;
2. call `W6X_WiFi_DeInit()`;
3. call `W6X_DeInit()`;
4. verify `CHIP_EN` is low and RDY reaches the expected idle state;
5. wait the configured settling delay and return to `Off`;
6. after the configured restart delay, execute the complete initialization,
   netif creation, connection, DHCP, disconnect, and shutdown path again.

Teardown must also be safe after partial initialization. If symmetric teardown
cannot be proven, do not repeatedly call `MX_LWIP_Init()`. Retain W6X/LwIP,
disconnect between jobs, and use automatic standby while documenting full
shutdown as blocked.

Acceptance check: at least 20 full shutdown and cold-reinitialization cycles
pass without heap loss, task growth, stale callbacks, transport failure, or
host reset.

#### Phase 3 bench validation result (2026-08-22 to 2026-08-23)

One manual station lifecycle was completed successfully from a cold host
boot using the local ignored credentials. The ST67 reported middleware 1.3.0
and SDK 2.0.106. The observed sequence was:

- `W6X_Init`, callback registration, `W6X_WiFi_Init`, and `MX_LWIP_Init`
   completed successfully.
- The station associated with SSID `lemo` on channel 5 at RSSI `-35`.
- Host LwIP DHCP assigned `192.168.1.142` with netmask `255.255.255.0`,
   gateway `192.168.1.1`, and DNS server `192.168.1.1`.
- `W6X_WiFi_Disconnect(1)` completed, the station link went down, and
   repeatable ST67 netif teardown returned status 0.
- The cycle completed without transport drops, RX overflow, or RX truncation.

Resource measurements during the run were:

| Measurement | Result |
|---|---:|
| Initial free heap | 39,080 B |
| Lowest-ever free heap | 21,896 B |
| Post-cycle free heap | 33,720 B |
| ST67 service stack | 2,560 B configured, 720 B remaining |
| Debug service stack | 1,536 B configured, 632 B remaining |

The run satisfies the single-cycle functional checks and the Phase 3 heap
and stack margins. Subsequent three-cycle full-shutdown validation kept
explicit LwIP, Wi-Fi, SPI, command-handler, and modem allocation counters
balanced, with zero outstanding pbufs, a stopped netif worker, eight tasks,
and final `CHIP_EN=0`/`RDY=0`. The command-handler mutex cleanup removed the
large recurring leak seen before that fix.

A residual approximately 64-byte-per-cycle free-heap decline was still seen
in an earlier repeated full-shutdown run, so R2 is not considered closed
despite the balanced known-owner counters. The 100 persistent
connect/DHCP/disconnect cycles, 20 cold shutdown/reinitialization cycles, and
electrical verification of final `CHIP_EN` and `SPI_RDY` levels remain
outstanding. The explicit compile-time R3 lifecycle mode is now implemented;
the existing full-shutdown path remains the default while persistent and cold
restart bench acceptance is completed.

The subsequent 20-cycle cold-restart smoke capture reported `20/20` passed
cycles, zero debug transport drops/overflows/truncations, zero pbufs at
teardown, removed netifs, a stopped worker, and final `CHIP_EN=0`/`RDY=0`.
Its heap values were 39,080 B at batch start, 32,680 B at batch end, and
20,664 B minimum-ever. The aggregate task count changed from 7 to 8, so the
no-task-growth gate remains open. Persistent three-cycle evidence and the
formal cold-restart acceptance are still outstanding; the source default was
restored to `SingleFullShutdown` after the smoke run.

A repeated 20-cycle cold-restart run again completed `20/20` cycles without a
reported lifecycle failure. Every cycle reached zero pbufs, removed netifs,
a stopped worker, and `CHIP_EN=0`/`RDY=0`; task count returned to 8 after each
teardown. Post-cycle free heap declined from 33,896 B after cycle 1 to
27,864 B after cycle 20, with 15,744 B minimum-ever. This confirms an
unresolved full-shutdown resource-loss trend despite balanced visible teardown
counters. Eleven pre-existing USB `busyDrop` events did not increase; RX
overflow and truncation were zero. The functional cold-restart path passes,
but its resource-stability acceptance remains open.

The planned 100-cycle persistent run then completed `100/100` cycles without a
reported lifecycle failure. W6X, Wi-Fi, and host LwIP initialized once and
remained active between iterations; all cycles passed DHCP, disconnect callback,
link-down, and IPv4-clearing checks. The final teardown reported zero pbufs,
removed netifs, a stopped worker, and `CHIP_EN=0`/`RDY=0`. Free heap remained
at 23,984 B after each cycle, with 21,552 B minimum-ever; the active task count
remained 11 and returned to 8 after teardown. Debug transport drops, RX
overflow, and RX truncation were zero. R4 persistent acceptance passes; the
failure-path checks, cold-restart baseline, electrical verification, and R2
heap-stability gate remain open.

An incorrect-password single-cycle failure test returned status `2 (ERROR)`
from the bounded `connect` stage and preserved `connect/2` in the final
aggregate result. Cleanup reached zero pbufs, removed netifs, stopped the
worker, and `CHIP_EN=0`/`RDY=0`; debug drops, RX overflow, and RX truncation
were zero. Free heap after cleanup was 33,896 B with 22,328 B minimum-ever.
This failure-path case passes; the remaining R5 scenarios are not yet
complete.

A missing-credentials single-cycle failure returned
`credentials-unavailable` with status `0` before any W6X initialization.
Heap remained 39,080 B, minimum-ever heap remained 39,080 B, and task count
remained 7. The final aggregate preserved the original stage and cleanup did
not allocate or retain ST67 resources. The remaining R5 cases are not yet
complete.

An AP-unavailable single-cycle failure test returned status `2 (ERROR)` from
the bounded `connect` stage after approximately 3.6 seconds. The primary
result remained `connect/2`; cleanup reached zero pbufs, removed netifs,
stopped the worker, and `CHIP_EN=0`/`RDY=0`. A later retry succeeded after the
AP became available. No RX overflow or truncation occurred, although five
USB `busyDrop` events were present before the lifecycle and did not increase
during it. This failure-path case passes functionally; the remaining R5 cases
and a clean transport-counter rerun remain open.

The remaining R5 failure scenarios are intentionally deferred for now because
they are difficult to reproduce reliably on the bench. They remain open and
are not treated as passed; the completed failure cases are missing
credentials, incorrect password, and AP unavailable with recovery.

## 6. Error handling

Every active-stage failure proceeds through the same cleanup policy:

1. preserve the original failed stage and status;
2. request disconnect if association may exist and the driver responds;
3. stop DHCP and bring the station link down;
4. tear down repeatable netif resources if they were created;
5. deinitialize Wi-Fi and W6X only when their corresponding initialization
   completed;
6. report cleanup status and final GPIO state;
7. require a known `Ready`, `Off`, or terminal `Fault` state before accepting
   another trigger.

Do not hide a leak or partial teardown by retrying indefinitely. Escalate to a
host reset only after repeated transport-level failures and only when product
requirements permit it.

## 7. Observability and resource gates

Emit bounded `DebugService` records containing:

- cycle ID and state transition;
- stable stage, W6X status, LwIP status, and connection reason;
- SSID, channel, RSSI, security, and protocol, but never password;
- IPv4 address, netmask, gateway, and DNS server;
- association, DHCP, disconnect, teardown, and restart elapsed time;
- free heap, minimum-ever heap, task count, and relevant stack watermarks;
- final `complete` or `fault` result and final `CHIP_EN`/RDY levels.

Phase 3 resource gates:

- minimum-ever free heap remains at least 8 KB;
- every observed task retains a nonzero stack margin;
- the service task and `DebugService` each retain at least 256 bytes;
- no downward free-heap trend or upward task-count trend appears across the
  stress tests;
- no debug queue overflow obscures a final lifecycle result.

## 8. Verification matrix

### Build checks

1. Build `Debug-HostController` from a clean CMake graph.
2. Build `Debug-DisplayController` if shared configuration or headers changed.
3. Confirm no credentials appear in tracked files, binaries, maps, or logs.
4. Record flash, `.data`, `.bss`, and stack/heap deltas from Phase 2.

### Functional checks

1. Missing, empty, oversized, correct, and incorrect credentials.
2. Access point unavailable and disappearing during association.
3. DHCP server normal, delayed, and absent.
4. Unexpected disconnect before and after DHCP.
5. Repeated trigger while a cycle is active.
6. Disconnect with idle data plane.
7. Partial initialization failure at each allocation or task-creation boundary.
8. Repeated teardown invocation.

### Stress and electrical checks

1. Run 100 persistent-stack connect/DHCP/disconnect cycles.
2. Run at least 20 full shutdown/reinitialization cycles.
3. Capture SPI, CS, RDY, and `CHIP_EN` during disconnect and shutdown.
4. Confirm `CHIP_EN` low reaches the expected module shutdown current and no
   host GPIO back-powers the module.
5. Confirm resource minima and task count remain stable throughout both tests.

## 9. Expected file changes

| File or area | Planned change |
|---|---|
| `Appli/App/app_config.h` | Non-secret Phase 3 timeouts and cycle policy |
| Local credential template and ignore rules | Compile-safe, untracked bench credential boundary |
| `User/Inc/HostController/St67ServiceTask.hpp` | Connectivity-cycle command/result boundary |
| `User/Src/HostController/St67ServiceTask.cpp` | Connect, DHCP, disconnect, cleanup, and restart states |
| `User/Src/HostController/AppVariant.cpp` | Bind the manual Phase 3 lifecycle trigger |
| `LWIP/App/lwip.h` and `lwip.c` | Host DHCP status snapshot and symmetric netif lifecycle |
| `LWIP/App/lwip_netif.h` and `lwip_netif.c` | Idempotent netif task/callback teardown |
| `Core/Inc/FreeRTOSConfig.h` | Heap adjustment only if measurements require it |


#### Phase 3 bench validation result (2026-08-22 to 2026-08-23)

One manual station lifecycle was completed successfully from a cold host
boot using the local ignored credentials. The ST67 reported middleware 1.3.0
and SDK 2.0.106. The observed sequence was:

- `W6X_Init`, callback registration, `W6X_WiFi_Init`, and `MX_LWIP_Init`
   completed successfully.
- The station associated with SSID `lemo` on channel 5 at RSSI `-35`.
- Host LwIP DHCP assigned `192.168.1.142` with netmask `255.255.255.0`,
   gateway `192.168.1.1`, and DNS server `192.168.1.1`.
- `W6X_WiFi_Disconnect(1)` completed, the station link went down, and
   repeatable ST67 netif teardown returned status 0.
- The cycle completed without transport drops, RX overflow, or RX truncation.

Resource measurements during the run were:

| Measurement | Result |
|---|---:|
| Initial free heap | 39,080 B |
| Lowest-ever free heap | 21,896 B |
| Post-cycle free heap | 33,720 B |
| ST67 service stack | 2,560 B configured, 720 B remaining |
| Debug service stack | 1,536 B configured, 632 B remaining |

The run satisfies the single-cycle functional checks and the Phase 3 heap
and stack margins. Subsequent three-cycle full-shutdown validation kept
explicit LwIP, Wi-Fi, SPI, command-handler, and modem allocation counters
balanced, with zero outstanding pbufs, a stopped netif worker, eight tasks,
and final `CHIP_EN=0`/`RDY=0`. The command-handler mutex cleanup removed the
large recurring leak seen before that fix.

A residual approximately 64-byte-per-cycle free-heap decline was still seen
in an earlier repeated full-shutdown run, so R2 is not considered closed
despite the balanced known-owner counters. The 100 persistent
connect/DHCP/disconnect cycles, 20 cold shutdown/reinitialization cycles, and
electrical verification of final `CHIP_EN` and `SPI_RDY` levels remain
outstanding. The explicit compile-time R3 lifecycle mode is now implemented;
the existing full-shutdown path remains the default while persistent and cold
restart bench acceptance is completed.
