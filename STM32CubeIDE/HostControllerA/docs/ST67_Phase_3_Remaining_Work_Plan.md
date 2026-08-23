# ST67W611M1 Phase 3 Remaining Work Plan

## 1. Purpose

This plan starts from the successful 2026-08-22 single-cycle bench result and
covers only the work still required to close Phase 3. HTTP, TLS, response
parsing, and daily scheduling remain Phase 4 or later.

The current implementation proves this path once:

```text
cold W6X init
  -> Wi-Fi init
  -> create host LwIP netifs
  -> associate
  -> host DHCP
  -> disconnect
  -> remove netifs
  -> Wi-Fi/W6X deinit
  -> CHIP_EN low
```

The remaining work must prove two distinct lifecycle modes:

1. **Persistent mode:** initialize W6X, Wi-Fi, and LwIP once, then run 100
   connect/DHCP/disconnect cycles without recreating those resources.
2. **Full-shutdown mode:** recreate and destroy the ST67-facing resources and
   cold-start the module for at least 20 cycles.

Do not combine these into one stress result. They test different failure and
resource-retention mechanisms.

## 2. Current baseline

The accepted single-cycle baseline is:

| Measurement | Result |
|---|---:|
| Initial free heap | 39,080 B |
| Lowest-ever free heap | 21,896 B |
| Post-cycle free heap | 33,720 B |
| ST67 service stack | 2,560 B configured, 720 B remaining |
| Debug service stack | 1,536 B configured, 632 B remaining |
| Final lifecycle result | Complete |
| Transport errors | None observed |

The station received `192.168.1.142/24`, gateway `192.168.1.1`, and DNS
`192.168.1.1`. This is a reference result, not a fixed-address requirement.

## Current implementation status (2026-08-23)

R1 is implemented and the current single-cycle path produces authoritative
bounded results. It checks disconnect event completion, link-down state, IPv4
clearing, netif and worker teardown, final GPIO state, task count, and the
first failed stage. The service task uses a 2,560-byte static stack; the last
single-cycle measurement retained 720 bytes, and DebugService retained 632
bytes from its 1,536-byte stack.

R2 teardown ownership is instrumented and the latest three successful full
shutdown cycles showed balanced explicit owners:

```text
lwip net=6/6 tm=3/3 ap=0/0
wifi ctx=3/3 evt=3/3
spi evt=3/3 q=12/12
cmd=6/6 out=0
modem sem=9/9 buf=3/3
```

The same run kept `pbufs=0`, stopped the netif worker, removed the netifs,
held the task count at 8, and ended with `CHIP_EN=0` and `RDY=0`. These results
close the previously observed command-handler mutex leak and confirm the
known teardown owners, but they do not close the R2 heap-stability gate: an
earlier repeated full-shutdown run still showed an unresolved approximately
64-byte-per-cycle free-heap decline. The explicit counters do not identify
that residual as an unmatched owner; allocator fragmentation or an
uninstrumented internal LwIP/RTOS object remains possible. IPv6 and a
one-second stabilization delay did not explain it.

R3 lifecycle mode logic is now implemented behind the compile-time policy in
`app_config.h`. Persistent mode initializes W6X, Wi-Fi, and the host LwIP
interfaces once, then repeats connect/DHCP/disconnect without calling
`MX_LWIP_DeInit()`, `W6X_WiFi_DeInit()`, or `W6X_DeInit()` between cycles.
Full-shutdown mode remains the default. The three-cycle persistent acceptance
run, failure-path checks, and formal cold-restart test remain outstanding; do
not mark R2 complete until its heap-stability gate is closed.

The 20-cycle cold-restart smoke capture completed `20/20` cycles with no
reported lifecycle failures. It observed zero pbufs, stopped workers, removed
netifs, and final `CHIP_EN=0`/`RDY=0`; debug transport drops, RX overflow, and
RX truncation were all zero. Free heap was 39,080 B at batch start, 32,680 B
at batch end, with 20,664 B observed as the minimum-ever value. The aggregate
task count was 7 at batch start and 8 at batch end, leaving the task-count gate
open. The persistent three-cycle acceptance run is not represented by this
capture, so R4 and formal R3 closure remain outstanding. The checked-in mode
was restored to `SingleFullShutdown` afterward.

A repeated 20-cycle cold-restart run again completed `20/20` cycles with no
reported lifecycle failures. Every cycle reached zero pbufs, removed netifs,
a stopped worker, and `CHIP_EN=0`/`RDY=0`; task count returned to 8 after each
teardown. Post-cycle free heap declined from 33,896 B after cycle 1 to
27,864 B after cycle 20, with 15,744 B minimum-ever. This confirms an
unresolved full-shutdown resource-loss trend even though the visible teardown
counters remain balanced. Eleven pre-existing USB `busyDrop` events did not
increase; RX overflow and truncation were zero. The functional cold-restart
path passes, but the resource-stability gate remains open.

## 3. Increment R1 - Make lifecycle results authoritative

Before stress testing, close the gaps where the current implementation logs a
successful cycle without checking every required postcondition.

### Implementation

1. Add a monotonic cycle ID and a stable result structure containing:
   - lifecycle mode;
   - first failed stage;
   - W6X status;
   - Wi-Fi reason;
   - LwIP status;
   - association, DHCP, disconnect, and teardown elapsed times;
   - heap and task-count values before and after the cycle.
2. Preserve the first operational failure while recording cleanup failures
   separately. Cleanup must not overwrite the original cause.
3. Check the return from the application disconnect wait. A successful
   `W6X_WiFi_Disconnect(1)` call is insufficient if the disconnected callback
   is not observed before the timeout.
4. After disconnect, require all of the following before reporting success:
   - station state is disconnected;
   - station netif link is down;
   - station IPv4 address is zero;
   - no unexpected reconnect event occurred during a short guard interval.
5. After full teardown, require:
   - station and AP netif pointers are null;
   - netif worker task handle is null;
   - `CHIP_EN` reads low;
   - `SPI_RDY` has the documented idle level after the settling delay.
6. Log one bounded final record per cycle. Do not rely on multiple intermediate
   lines to determine pass or fail.
7. Add task count to resource telemetry with `uxTaskGetNumberOfTasks()` and
   capture it at the same points as free heap.

### Acceptance

- A successful cycle cannot be reported if disconnect, link-down, address
  clearing, netif removal, or final GPIO checks fail.
- Every failure has one stable primary stage and optional cleanup status.
- Final records fit in the debug queue without truncation or drops.

## 4. Increment R2 - Harden repeatable netif teardown

The current teardown removes the LwIP netifs and worker task, but repeated
cold recreation must not be accepted until callback, queue, and packet-buffer
ownership is explicit.

### Implementation

1. Add an outstanding custom-pbuf counter in `lwip_netif.c`:
   - increment after a driver buffer is wrapped successfully;
   - decrement only from the custom pbuf free callback;
   - prevent underflow;
   - expose a read-only count for diagnostics.
2. During teardown:
   - block new RX notifications;
   - clear driver callbacks;
   - stop the netif worker;
   - wait with a bounded timeout for outstanding custom pbufs to reach zero;
   - only then remove and free the LwIP netifs and deinitialize W6X.
3. If pbufs remain at timeout, fail at `netif-drain` and do not free resources
   that can still be referenced.
4. Add rollback for every `MX_LWIP_Init()` boundary, including failure after
   `W6X_Netif_Init()` but before netif task creation.
5. Verify the vendor SPI queue lifecycle. `W6X_Netif_DeInit()` clears link
   callbacks but does not explicitly unbind station/AP queues. Confirm by
   source inspection and repeated tests that `W6X_DeInit()`/
   `BusIo_SPI_DeInit()` destroys all queue bindings before the next
   `W6X_Netif_Init()`. If it does not, add the smallest symmetric unbind API at
   the vendor-port boundary.
6. Make `MX_LWIP_DeInit()` and `net_if_deinit()` idempotent and test calling
   each twice after:
   - complete initialization;
   - partial initialization;
   - failed association;
   - successful DHCP.
7. Keep the TCP/IP core alive. Do not call `tcpip_init()` more than once and do
   not attempt to delete the LwIP TCP/IP task.

### Acceptance

- Ten create/destroy harness cycles return post-cycle free heap and task count
  to the same steady-state values after the first cycle.
- Outstanding pbuf count is zero before every driver shutdown.
- A second teardown call succeeds without callbacks, frees, or task deletion
  against stale resources.
- No callback executes against a removed netif.

## 5. Increment R3 - Add explicit bench lifecycle modes

Add a compile-time bench policy in `app_config.h`; do not place it in the local
credential header.

The implementation-ready design, code boundaries, staged bench checks, and
R3 exit criteria are defined in
[`ST67_R3_Implementation_Plan.md`](ST67_R3_Implementation_Plan.md).

```text
SingleFullShutdown
PersistentStress
ColdRestartStress
```

Keep production default behavior explicit and keep stress modes disabled in
normal builds.

### PersistentStress behavior

1. Initialize W6X, Wi-Fi, and LwIP once.
2. For each cycle:
   - connect;
   - wait for host DHCP;
   - capture diagnostics;
   - disconnect with `restore=1`;
   - verify link-down and cleared IPv4 address;
   - return to `Ready` without calling `MX_LWIP_DeInit()`,
     `W6X_WiFi_DeInit()`, or `W6X_DeInit()`.
3. Wait a configurable short interval before the next cycle.
4. Perform one final full teardown after the requested cycle count.

### ColdRestartStress behavior

1. Run the complete currently implemented lifecycle.
2. Verify final GPIO state.
3. Wait the configured cold-restart delay.
4. Start again from `W6X_Init()`.
5. Stop immediately on the first failed cycle and retain its diagnostics.

### Harness controls

- Use fixed compile-time cycle counts: 100 persistent and 20 cold restart.
- A switch press starts the selected batch; another press while active is
  rejected without altering the batch.
- Emit a concise per-cycle summary and a final aggregate. Avoid repeating the
  full module-information dump after cycle 1.
- Include pass count, failure count, first failed cycle, starting/current free
  heap, minimum-ever heap, task count, and stack minima.

## 6. Increment R4 - Run 100 persistent cycles

Run the persistent harness before cold-restart stress. This separates Wi-Fi,
DHCP, and disconnect stability from resource-recreation defects.

### Test conditions

1. Keep the AP and DHCP server stable for the primary run.
2. Keep USB CDC statistics enabled.
3. Record every cycle's:
   - association and DHCP times;
   - disconnect event and link-down result;
   - free heap after disconnect;
   - task count;
   - service, DebugService, SPI, modem RX, netif, and TCP/IP stack minima;
   - debug transport drop counters.
4. Save the complete log and a compact result table.

### Acceptance

  retains a nonzero margin.

If this test fails, stop before cold-restart testing and classify the first
failure as association, DHCP, disconnect, stale link/address, resource trend,
or callback leakage.

An incorrect-password single-cycle run produced the expected bounded
`connect` failure with status `2 (ERROR)`. The primary aggregate result
preserved `connect/2`; cleanup completed with zero pbufs, stopped worker,
removed netifs, and `CHIP_EN=0`/`RDY=0`. Debug transport drops, RX overflow,
and RX truncation were zero. Free heap was 33,896 B after cleanup with
22,328 B minimum-ever, and the service and DebugService margins were 848 B
and 640 B. This closes the incorrect-password smoke case; the remaining R5
failure scenarios are still required.

A missing-credentials single-cycle run failed before W6X initialization with
`credentials-unavailable` and status `0`. No W6X, Wi-Fi, or LwIP resources
were allocated; free and minimum-ever heap stayed at 39,080 B and task count
stayed at 7. The aggregate result preserved `credentials-unavailable` and
the next trigger remained available. The remaining R5 scenarios are still
required.

The remaining failure-path scenarios are intentionally deferred for now
because they require difficult bench conditions to reproduce reliably. They
remain open acceptance items rather than passed results. The completed cases
are missing credentials, incorrect password, and AP unavailable with later
recovery.

An AP-unavailable single-cycle run produced the expected bounded `connect`
failure with status `2 (ERROR)` after approximately 3.6 seconds. Cleanup
completed with zero pbufs, stopped worker, removed netifs, and
`CHIP_EN=0`/`RDY=0`; a later retry succeeded after the AP became available.
No RX overflow or RX truncation occurred. Five USB `busyDrop` events were
present before the lifecycle and did not increase during the test, so the
failure-path lifecycle passed but the debug transport quality gate should be
repeated with a clean counter baseline. The remaining R5 scenarios are still
required.

The planned 100-cycle persistent run completed `100/100` cycles with no
reported lifecycle failures. Initialization occurred once, cycles 1 through
100 retained the live W6X/Wi-Fi/LwIP resources, and one final teardown removed
the netifs and stopped the worker. Every cycle passed DHCP, disconnect callback,
link-down, and IPv4-clearing checks. Free heap stabilized at 23,984 B after
each cycle, with 21,552 B minimum-ever; the active batch task count remained
11 and returned to 8 after teardown. Debug transport drops, RX overflow, and
RX truncation were zero. This closes the R4 persistent functional/resource
run; failure-path checks, cold-restart baseline work, electrical verification,
and the R2 heap-stability gate remain open.

## 7. Increment R5 - Validate bounded failure paths

Run failure tests with a cycle count of one before the 20-cycle cold restart
run.

| Scenario | Expected primary result |
|---|---|
| Missing local credential header | `credentials-unavailable` before W6X connect |
| Empty or oversized credential | `credentials-invalid` |
| Incorrect password | bounded `connect` failure with reason |
| AP unavailable | bounded `connect` failure |
| DHCP unavailable | bounded `dhcp` failure followed by cleanup |
| AP removed after DHCP | disconnected event and bounded cleanup |
| Trigger while active | trigger rejected; active cycle unchanged |
| Repeated teardown | both calls return safely |

For allocation and task-creation rollback, add temporary compile-time fault
injection at each `MX_LWIP_Init()` boundary. Do not keep runtime fault
injection enabled in production builds.

### Acceptance

- Every scenario terminates in bounded time.
- The primary failure is preserved through cleanup.
- The next valid manual cycle can start from a known state.
- No scenario leaks heap or increases task count.

Current decision: defer the remaining R5 bench scenarios and continue with the
validated normal lifecycle path. Do not claim full R5 or formal Phase 3
closure until those cases are run.

## 8. Increment R6 - Run 20 cold shutdown/reinitialization cycles

Enable `ColdRestartStress` only after R2, R4, and R5 pass.

### Acceptance

- 20 of 20 cycles complete from `W6X_Init()` through DHCP, disconnect,
  teardown, and `W6X_DeInit()`.
- `CHIP_EN` is low and `SPI_RDY` is at the expected idle level between cycles.
- No stale SPI callback or queue binding survives into the next cycle.
- Outstanding pbuf count is zero before every shutdown.
- Post-cycle free heap and task count are stable.
- No transport failure or host reset occurs.

A first-cycle/second-cycle failure strongly indicates stale callback, task,
queue, or driver-global state and must be resolved before continuing the
batch.

## 9. Increment R7 - Electrical and power verification

Use a logic analyzer and current measurement; firmware logs alone cannot close
this gate.

1. Capture SPI clock, CS, RDY, and `CHIP_EN` for:
   - cold startup;
   - active traffic;
   - disconnect;
   - W6X deinitialization;
   - the interval before cold restart.
2. Verify CS is deasserted before `CHIP_EN` goes low.
3. Verify RDY reaches its expected idle level within the configured shutdown
   settling timeout.
4. Measure module current in shutdown and compare it with the board-level
   expectation. The module's 200 nA typical value is not the expected total
   board current.
5. Check that SPI, CS, BOOT, and RDY GPIO states do not back-power the module
   while `CHIP_EN` is low.
6. Save captures with cycle ID and firmware build identification.

### Acceptance

- GPIO sequencing matches the driver lifecycle on every captured transition.
- Shutdown current is repeatable and no GPIO back-power path is observed.
- The measured result supports selecting full shutdown for the daily workflow.

If electrical shutdown cannot be validated, retain persistent initialization
and automatic standby as the documented fallback power policy.

## 10. Phase 3 closure checklist

Phase 3 is complete only when all items below are recorded:

- [ ] Lifecycle postconditions and first-failure reporting are authoritative.
- [ ] Netif teardown accounts for outstanding pbufs and queue/callback state.
- [ ] Ten teardown harness cycles pass.
- [ ] 100 persistent connect/DHCP/disconnect cycles pass.
- [ ] Required failure scenarios terminate cleanly.
- [ ] 20 cold shutdown/reinitialization cycles pass.
- [ ] Heap, task count, and stack margins satisfy the resource gates.
- [ ] Logic-analyzer and shutdown-current evidence is saved.
- [ ] Final power policy is recorded as full shutdown or automatic standby.
- [ ] Temporary stress and fault-injection settings are disabled in the normal
      product build.
- [ ] Both Debug firmware variants build and no credential is tracked or
      present in committed artifacts.

After closure, update the dated validation sections in the Phase 3 and daily
fetch plans with aggregate results, not full raw logs.
