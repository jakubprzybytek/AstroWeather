# ST67W611M1 R3 Lifecycle Modes Implementation Plan

## 1. Objective

Add explicit compile-time lifecycle modes to `St67ServiceTask` without
changing the validated connection, DHCP, disconnect, or full-shutdown
postconditions.

R3 must support three policies:

```text
SingleFullShutdown
PersistentStress
ColdRestartStress
```

The first bench target is a three-cycle `PersistentStress` batch. After that
passes, change only the configured persistent cycle count to 100 for R4.
`SingleFullShutdown` remains the normal source default. R3 does not close the
open R2 full-shutdown heap-stability gate.

HTTP, TLS, payload handling, scheduling, runtime mode selection, and power
policy selection remain outside this increment.

## Bench result (2026-08-23)

The configured 20-cycle `ColdRestartStress` run completed `20/20` cycles
without a reported lifecycle failure. Each visible cycle ended with zero
outstanding pbufs, stopped netif worker, removed station/AP netifs, and
`CHIP_EN=0`/`RDY=0`. Debug transport counters remained at zero drops,
overflows, and truncations. The observed service stack margin was 848 bytes
and the DebugService margin was 640 bytes.

The run started at 39,080 bytes free heap and ended at 32,680 bytes free heap;
the observed minimum-ever free heap was 20,664 bytes. The aggregate record
reported task count changing from 7 to 8, so the no-task-growth gate still
needs explanation or a controlled baseline before cold-restart acceptance is
closed. This capture does not provide the required three-cycle persistent
batch record, and does not close R2's separate full-shutdown heap gate.

The checked-in source default has been restored to `SingleFullShutdown` after
the cold-restart run.

A repeated 20-cycle `ColdRestartStress` run again completed `20/20` cycles
without a reported lifecycle failure. Every cycle reached zero pbufs, removed
station/AP netifs, a stopped worker, and `CHIP_EN=0`/`RDY=0`; task count stayed
at 8 after teardown. However, post-cycle free heap declined from 33,896 bytes
after cycle 1 to 27,864 bytes after cycle 20, while minimum-ever heap reached
15,744 bytes. This confirms a significant unresolved full-shutdown resource
loss despite balanced visible teardown counters. Eleven pre-existing USB
`busyDrop` events did not increase during the run; RX overflow and truncation
remained zero. The cold-restart functional path passes, but its resource
stability gate remains open.

The subsequent three-cycle `PersistentStress` run completed `3/3` cycles
without a reported lifecycle failure. W6X, Wi-Fi, and host LwIP initialized
once; cycles 2 and 3 retained the live netifs and worker, and only the final
batch teardown deinitialized them. Every cycle observed successful DHCP,
disconnect callback completion, link-down, and cleared IPv4 state. The batch
ended with zero pbufs, removed netifs, a stopped worker, and
`CHIP_EN=0`/`RDY=0`; debug transport drops, RX overflow, and RX truncation
were zero.

Persistent resources stabilized at 23,984 bytes free heap after each cycle,
with 21,856 bytes as the minimum-ever value during the batch. The service
stack retained 848 bytes and DebugService retained 640 bytes. The batch task
count was 11 while the stack was active and 8 after teardown; the overall
batch record was 7 to 8 because the persistent LwIP core remains outside this
teardown boundary. The short R3 persistent run passes its functional gate,
and `APP_ST67_PERSISTENT_STRESS_CYCLES` is now set to `100U` for R4. The
formal 100-cycle run, failure-path checks, cold-restart resource baseline, and
R2 heap-stability gate remain open.

An incorrect-password single-cycle run subsequently failed at the bounded
`connect` stage with status `2 (ERROR)` and preserved `connect/2` as the
aggregate first failure. Best-effort cleanup completed successfully: pbufs
were zero, the netif worker stopped, station/AP netifs were removed, and
`CHIP_EN=0`/`RDY=0`. Debug transport drops, RX overflow, and RX truncation
were zero. Free heap was 33,896 bytes after cleanup with 22,328 bytes as the
minimum-ever value; service and DebugService margins remained 848 and 640
bytes. This closes the incorrect-password smoke case; the other R5 failure
scenarios remain open.

An AP-unavailable single-cycle run subsequently failed at the bounded
`connect` stage with status `2 (ERROR)` after approximately 3.6 seconds.
Best-effort cleanup completed successfully: pbufs were zero, the netif worker
stopped, station/AP netifs were removed, and `CHIP_EN=0`/`RDY=0`. A later
manual retry succeeded after the AP was available again, confirming recovery
from the bounded failure. No RX overflow or RX truncation occurred; five USB
`busyDrop` events were already present before the lifecycle and remained
unchanged during it. This closes the AP-unavailable smoke case functionally;
the remaining R5 failure scenarios remain open.

A missing-credentials single-cycle run failed before W6X initialization with
`credentials-unavailable` and status `0`. No W6X, Wi-Fi, or LwIP resources
were allocated: heap and minimum-ever heap remained 39,080 bytes, and task
count remained 7. Cleanup was idempotent and the aggregate result preserved
`credentials-unavailable`; the remaining R5 failure scenarios remain open.

The planned 100-cycle `PersistentStress` run subsequently completed `100/100`
cycles without a reported lifecycle failure. W6X, Wi-Fi, and host LwIP
initialized once; every cycle passed DHCP, disconnect callback completion,
link-down, and IPv4 clearing, while the live netif worker and netifs remained
present between cycles. The final teardown observed zero pbufs, removed
netifs, a stopped worker, and `CHIP_EN=0`/`RDY=0`. Debug transport drops, RX
overflow, and RX truncation were zero.

Free heap stabilized at 23,984 bytes after each cycle, with 21,552 bytes as
the batch minimum-ever value. The service stack retained 872 bytes and
DebugService retained 664 bytes at the initial report, later retaining 656
bytes; the active batch task count remained 11 and returned to 8 after
teardown. The R4 persistent functional and resource run passes. Failure-path
checks, the cold-restart resource baseline, electrical verification, and the
separate R2 full-shutdown heap gate remain open. The checked-in source default
has been restored to `SingleFullShutdown`.

## 2. Current control path

`St67ServiceTask::runConnectivityCycle()` currently owns one complete cold
lifecycle. It initializes W6X, Wi-Fi, and the host LwIP interfaces; connects;
waits for host DHCP; disconnects; and then unconditionally calls:

```text
MX_LWIP_DeInit()
W6X_WiFi_DeInit()
W6X_DeInit()
```

The existing `cleanup(bool disconnect)` therefore combines two boundaries
that R3 must control independently:

1. End one station iteration by disconnecting and proving link-down and IPv4
   clearing.
2. Destroy the persistent W6X/Wi-Fi/LwIP resources and prove final GPIO state.

The public LwIP APIs already provide the required checks:
`lwip_get_station_status()`, `lwip_netifs_are_removed()`,
`net_if_is_running()`, and `net_if_outstanding_pbufs()`.

## 3. Configuration boundary

Add the lifecycle policy and bench counts to `Appli/App/app_config.h`. Keep
them separate from `app_credentials.h` and use named values rather than raw
numeric conditions in the service.

```c
#define APP_ST67_LIFECYCLE_SINGLE_FULL_SHUTDOWN 0U
#define APP_ST67_LIFECYCLE_PERSISTENT_STRESS    1U
#define APP_ST67_LIFECYCLE_COLD_RESTART_STRESS  2U

#ifndef APP_ST67_LIFECYCLE_MODE
#define APP_ST67_LIFECYCLE_MODE \
  APP_ST67_LIFECYCLE_SINGLE_FULL_SHUTDOWN
#endif

#define APP_ST67_PERSISTENT_STRESS_CYCLES 3U
#define APP_ST67_COLD_RESTART_STRESS_CYCLES 20U
#define APP_ST67_INTER_CYCLE_DELAY_MS 1000U
```

Add a compile-time check in `St67ServiceTask.cpp` that rejects unknown mode
values and zero stress counts. Do not depend on a CMake cache variable unless
the build later adds an explicit `target_compile_definitions()` mapping; the
source configuration must be sufficient by itself.

After the three-cycle persistent acceptance run, change
`APP_ST67_PERSISTENT_STRESS_CYCLES` from `3U` to `100U`. Do not alter lifecycle
logic between the short run and R4.

## 4. Service decomposition

Keep the implementation private to `St67ServiceTask.cpp`; no public header
change is required. Refactor the current monolithic path into four operations.

### 4.1 Initialize persistent resources

Extract an `initializeStack()` helper that performs the existing guarded
initialization in this order:

1. Validate credentials before starting the module.
2. Call `W6X_Init()` if W6X is not initialized.
3. Read and log module information only for the first initialization in a
   batch.
4. Register callbacks and call `W6X_WiFi_Init()` if Wi-Fi is not initialized.
5. Call `MX_LWIP_Init()` if the ST67-facing netifs are not initialized.
6. Require non-null station and AP netifs and a running netif worker.

Retain `w6xInitialized_`, `wifiInitialized_`, and `lwipInitialized_` as the
authoritative ownership flags. On partial failure, use the full teardown
helper for every resource whose ownership flag was set.

### 4.2 Run one station iteration

Extract a `runStationIteration()` helper that performs only:

```text
connect
  -> verify station connected
  -> wait for host DHCP
  -> capture network diagnostics
  -> disconnect with restore=1
  -> observe disconnected callback
  -> verify station disconnected, link down, and IPv4 clear
  -> run the existing reconnect guard
```

The helper must not call `MX_LWIP_DeInit()`, `W6X_WiFi_DeInit()`, or
`W6X_DeInit()`. A successful return leaves W6X, Wi-Fi, both LwIP netifs, and
the netif worker initialized and returns the service to `Ready`.

At the start of every iteration, clear operation-specific thread flags and
reset the per-iteration first-failure fields. Preserve the existing bounded
association, DHCP, disconnect, and reconnect checks.

On a failure after association may have started, perform the same bounded
best-effort disconnect verification before returning. Preserve the original
failure stage if disconnect cleanup also fails.

### 4.3 Perform final teardown

Extract a `shutdownStack()` helper from the resource-destruction portion of
`cleanup()`:

1. If station state may still be active, make one bounded best-effort
   disconnect attempt.
2. Call `MX_LWIP_DeInit()` and require zero pbufs, a stopped worker, and null
   station/AP netif pointers.
3. Call `W6X_WiFi_DeInit()` and clear `wifiInitialized_`.
4. Call `W6X_DeInit()` and clear `w6xInitialized_`.
5. Wait `APP_ST67_SHUTDOWN_SETTLING_DELAY_MS`.
6. Require `finalHardwareState()` to observe removed netifs, no worker,
   `CHIP_EN=0`, and `RDY=0`.

Clear each ownership flag only after its corresponding deinitializer has been
invoked. Keep teardown idempotent so partial initialization and a second
best-effort cleanup do not touch stale resources.

The persistent batch calls this helper exactly once, after its final
iteration or immediately after its first failed iteration.

### 4.4 Dispatch one batch

Replace `runConnectivityCycle()` with a mode-dispatching `runBatch()` while
retaining one switch press as the trigger.

| Mode | Initialization | Iterations | Teardown |
|---|---|---:|---|
| `SingleFullShutdown` | Once | 1 | Once after the iteration |
| `PersistentStress` | Once | Configured persistent count | Once after the batch |
| `ColdRestartStress` | Before every iteration | Configured cold count | After every iteration |

For persistent mode, wait `APP_ST67_INTER_CYCLE_DELAY_MS` between iterations
without deinitializing any stack resource. Before starting the next
iteration, require:

- service state `Ready`;
- initialized ownership flags still set;
- station disconnected;
- station and AP netif pointers still non-null;
- netif worker still running;
- zero outstanding custom pbufs after the settling interval.

For cold-restart mode, require the existing final hardware postconditions and
wait `APP_ST67_COLD_RESTART_DELAY_MS` before the next initialization. Stop a
stress batch on its first failed iteration, then finish any required teardown
and emit the aggregate result.

## 5. Results and diagnostics

Separate per-iteration state from batch state so resetting one iteration does
not erase the batch's first failure.

Add a compact private batch result containing at least:

```text
mode
requested cycles
attempted cycles
passed cycles
failed cycles
first failed cycle
first failed stage and status
starting and ending free heap
lowest observed free heap
starting and ending task count
lowest service and DebugService stack margins
```

The global `cycleId_` remains monotonic and identifies each station
iteration, not each button press. Add a monotonic batch ID if it fits the
existing bounded record format.

Emit:

1. One batch-start record containing mode, requested count, heap, and tasks.
2. One concise final record for every attempted iteration.
3. One batch-final record containing pass/fail counts, first failed cycle,
   heap start/end/minimum, tasks start/end, and teardown status.

Keep each `DebugService` record within its fixed formatting buffer. Retain the
compact allocation-owner summaries and teardown checkpoints, but emit the
full module-information record only after the first initialization in a
batch.

## 6. Trigger behavior

A switch press starts the configured batch. A press while a batch is active
must not queue another batch for execution afterward.

Add a task-owned `batchActive_` guard that is visible to `trigger()`. Set it
before beginning a batch and clear it only after final reporting. Because the
trigger originates through `SwitchTask`, keep rejection bounded and avoid
performing lifecycle work in the callback. Clear any stale trigger flag when
the accepted batch begins so a press during the batch cannot remain latched
as a second request.

Log one concise rejection record when practical from task context. Correct
rejection is more important than logging from interrupt context.

## 7. Implementation sequence

### Increment R3.1 - Add policy and split cleanup

1. Add named lifecycle values, the default policy, short persistent count,
   cold count, and inter-cycle delay to `app_config.h`.
2. Extract disconnect verification from `cleanup()` into the station
   iteration boundary.
3. Extract full resource destruction into `shutdownStack()`.
4. Keep `SingleFullShutdown` selected and confirm one switch press produces
   the same externally observed lifecycle and final checks as before.

Acceptance: both Debug variants build, the normal mode remains one full
shutdown, and its final record and GPIO/netif postconditions are unchanged.

### Increment R3.2 - Add persistent batch control

1. Add mode dispatch and aggregate counters.
2. Run `initializeStack()` once.
3. Run three station iterations with only disconnect verification between
   them.
4. Run `shutdownStack()` once after cycle 3 or after the first failure.
5. Reject triggers while the batch is active.

Acceptance: a single switch press runs exactly three iterations, cycles 2 and
3 do not log or call any W6X/Wi-Fi/LwIP initializer or deinitializer, all
three clear link and IPv4 state, and one final teardown leaves the existing
full-shutdown postconditions true.

### Increment R3.3 - Add cold-restart batch control

1. Compose the same helpers into repeated initialize/iterate/shutdown cycles.
2. Stop on the first failure.
3. Preserve the failed iteration's primary stage through final cleanup and
   aggregate reporting.

Acceptance: use a temporary count of two for the first bench check; both
iterations cold-initialize and fully shut down, with zero pbufs and stable
task count. Restore the configured cold count to 20 but do not run the formal
R6 batch until R2, R4, and R5 permit it.

### Increment R3.4 - Promote persistent count for R4

After the three-cycle persistent run passes, change only
`APP_ST67_PERSISTENT_STRESS_CYCLES` to `100U`, rebuild, and proceed with the
R4 test protocol. Do not combine the short R3 evidence with the formal R4
result.

## 8. Focused validation

Run validation in this order:

1. Build `Debug-HostController` with `SingleFullShutdown` selected.
2. Build `Debug-DisplayController` because `app_config.h` is shared.
3. Bench one default cycle and compare its stage order and final state with
   the accepted baseline.
4. Select `PersistentStress` with count 3 and rebuild both variants.
5. Start one batch and verify exactly three iteration-final records and one
   batch-final record.
6. Confirm initialization occurs only before cycle 1 and full teardown only
   after cycle 3.
7. Confirm every disconnect observes the callback, link-down, zero IPv4, no
   reconnect, zero pbufs, unchanged task count, and no downward post-iteration
   heap trend.
8. Press the switch during cycle 1 and confirm no second batch starts after
   the current batch ends.
9. Confirm final teardown reports removed netifs, stopped worker,
   `CHIP_EN=0`, and `RDY=0`.
10. Restore `SingleFullShutdown` as the checked-in default after bench
    validation unless the next immediate operation is the controlled R4 run.

Use the existing resource gates: minimum-ever heap at least 8 KB,
St67Service and DebugService stack margins at least 256 bytes, every other
task with a nonzero margin, no task-count growth, and no hidden final result
from debug truncation or queue overflow.

## 9. Files expected to change

| File | Change |
|---|---|
| `Appli/App/app_config.h` | Named policy, default mode, cycle counts, and inter-cycle delay |
| `User/Src/HostController/St67ServiceTask.cpp` | Helper split, mode dispatch, batch guard, aggregates, and bounded records |
| `docs/ST67_Phase_3_Remaining_Work_Plan.md` | Link this plan and record the R3 bench outcome after execution |
| `docs/ST67_Phase_3_Implementation_Plan.md` | Add the dated R3 result after validation |
| `docs/ST67_Daily_Fetch_Implementation_Plan.md` | Add only the aggregate result and resulting lifecycle decision |

No change is expected in `St67ServiceTask.hpp`, the LwIP public API, the
credential boundary, or generated CubeMX files unless implementation exposes
a concrete missing capability.

## 10. Exit criteria

R3 is complete when:

- all three named policies compile;
- `SingleFullShutdown` remains the source default and preserves its validated
  behavior;
- one switch press runs exactly one configured batch;
- an active batch cannot queue another batch;
- a three-cycle persistent batch initializes once, disconnects cleanly after
  every iteration, and tears down once;
- persistent iterations retain live netifs and the worker while showing zero
  stale IPv4 state, reconnect events, pbuf ownership, task growth, or heap
  trend;
- a two-cycle cold-restart smoke test composes the same helpers correctly;
- the first operational failure survives any cleanup failure;
- final aggregate records are complete and untruncated;
- both Debug firmware variants build without tracked credentials; and
- the code is ready for R4 by changing only the persistent cycle count from 3
  to 100.
