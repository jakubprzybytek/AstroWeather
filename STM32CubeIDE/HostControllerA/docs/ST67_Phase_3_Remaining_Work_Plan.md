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

- 100 of 100 cycles pass.
- No automatic reconnect occurs between cycles.
- Post-disconnect IPv4 address is zero on every cycle.
- Post-cycle free heap has no downward trend after initial pool allocation.
- Task count is constant.
- Minimum-ever free heap remains at least 8 KB.
- St67Service and DebugService each retain at least 256 B; every other task
  retains a nonzero margin.
- No final cycle result is hidden by debug queue overflow or truncation.

If this test fails, stop before cold-restart testing and classify the first
failure as association, DHCP, disconnect, stale link/address, resource trend,
or callback leakage.

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
