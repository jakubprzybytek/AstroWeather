# ST67W611M1 Phase 3 Connect, Stress, and Shutdown Plan

## 1. Current status summary

### Completed

- R1 authoritative lifecycle results are implemented.
- R2 repeatable ST67-facing LwIP teardown is implemented and instrumented.
- R3 compile-time lifecycle modes are implemented in `St67ServiceTask`:
  `SingleFullShutdown`, `PersistentStress`, and `ColdRestartStress`.
- The 100-cycle persistent stress run passed `100/100`.
- The 20-cycle cold-restart run passed functionally: `20/20` cycles completed,
  with zero pbufs, removed netifs, stopped workers, and final
  `CHIP_EN=0`/`RDY=0`.
- Missing credentials, incorrect password, and AP-unavailable recovery cases
  passed their functional checks.
- DebugService log records are now sized for 200 characters instead of 128.
- Both Debug firmware variants build successfully.

### Outstanding issues

- The full-shutdown path has a confirmed heap-loss trend. In the latest
  20-cycle cold run, post-cycle free heap declined from `33,896 B` after cycle
  1 to `27,864 B` after cycle 20; minimum-ever heap reached `15,744 B`.
- Visible allocation counters balance, pbufs return to zero, and task count
  returns to 8, so the remaining loss is likely an uninstrumented vendor,
  LwIP, RTOS, or allocator-fragmentation issue. It is not fixed yet.
- The cold run's functional result is good, but resource stability is not
  accepted until the heap trend is explained or a product policy explicitly
  avoids repeated full shutdown.
- Eleven USB `busyDrop` events were already present before the latest cold
  run and did not increase. A clean zero-baseline transport run is still
  desirable.
- Remaining difficult R5 failure scenarios are intentionally deferred:
  invalid/oversized credentials, DHCP failure, unexpected disconnect,
  repeated teardown, active-trigger rejection, and partial-init fault
  injection.
- Electrical verification of CS, RDY, CHIP_EN, shutdown current, and
  back-powering has not been completed.

### Conclusions

Persistent lifecycle operation is stable over 100 connect/DHCP/disconnect
iterations and is the current fallback for operation. Full shutdown is
functionally repeatable over 20 cycles but is not resource-stable. The known
command-handler mutex leak was fixed; the remaining full-shutdown loss is a
separate unresolved issue. Do not claim formal Phase 3 closure or close R2's
heap gate yet.

The checked-in source configuration remains:

```c
#define APP_ST67_LIFECYCLE_MODE APP_ST67_LIFECYCLE_SINGLE_FULL_SHUTDOWN
#define APP_ST67_PERSISTENT_STRESS_CYCLES 100U
#define APP_ST67_COLD_RESTART_STRESS_CYCLES 20U
```

## 2. Scope and constraints

Phase 3 covers a bounded station lifecycle:

```text
trigger
  -> initialize W6X, Wi-Fi, and host LwIP when required
  -> connect to the configured access point
  -> wait for host LwIP DHCP
  -> capture network diagnostics
  -> disconnect with restore=1
  -> verify link-down and cleared IPv4 state
  -> tear down ST67 netifs when required
  -> deinitialize Wi-Fi and W6X when required
  -> verify final hardware state
```

HTTP, HTTPS, response parsing, daily scheduling, runtime mode selection, and
final power-policy selection are outside this phase. The selected T02
architecture uses host LwIP for DHCP and network traffic; the T01-only W6X
got-IP event is not a connectivity gate.

`St67ServiceTask` is the sole application owner of the ST67 transport and
lifecycle. Callback functions only copy scalar event data and wake the task.
Only the task performs lifecycle operations and cleanup.

Credentials remain outside generated files. A missing local credential header
must compile and produce `credentials-unavailable` before W6X initialization.
Passwords must never be logged or committed.

## 3. Configuration

The non-secret lifecycle configuration is in `Appli/App/app_config.h`:

```c
#define APP_ST67_LIFECYCLE_SINGLE_FULL_SHUTDOWN 0U
#define APP_ST67_LIFECYCLE_PERSISTENT_STRESS    1U
#define APP_ST67_LIFECYCLE_COLD_RESTART_STRESS  2U

#ifndef APP_ST67_LIFECYCLE_MODE
#define APP_ST67_LIFECYCLE_MODE APP_ST67_LIFECYCLE_SINGLE_FULL_SHUTDOWN
#endif

#define APP_ST67_PERSISTENT_STRESS_CYCLES 100U
#define APP_ST67_COLD_RESTART_STRESS_CYCLES 20U
#define APP_ST67_INTER_CYCLE_DELAY_MS 1000U
```

The service has compile-time assertions for unknown modes and zero stress
counts. A bench stress mode is selected temporarily, then the checked-in
default is restored to `SingleFullShutdown`.

## 4. Implementation

The implementation is private to
`User/Src/HostController/St67ServiceTask.cpp`; the public header did not need
to change.

### Stack initialization

`initializeStack()` validates credentials, initializes W6X, registers
callbacks, initializes Wi-Fi, creates the ST67-facing LwIP netifs, and
requires non-null station/AP interfaces and a running netif worker. Ownership
flags are `w6xInitialized_`, `wifiInitialized_`, and `lwipInitialized_`.
Module information is logged only on the first initialization in a batch.

### Station iteration

`runStationIteration()` performs connect, station-state verification, host
DHCP waiting, network diagnostics, disconnect callback waiting, link-down and
IPv4 clearing checks, and a reconnect guard. It never deinitializes W6X, Wi-Fi,
or LwIP. Successful persistent iterations return to `Ready` with live netifs
and worker resources.

Failures preserve the first failed stage and status. If association may have
started, bounded best-effort disconnect is attempted before returning.

### Stack shutdown

`shutdownStack()` is idempotent. It disconnects if needed, calls
`MX_LWIP_DeInit()`, verifies pbuf, worker, and netif postconditions, then
deinitializes Wi-Fi and W6X, waits for settling, and verifies removed netifs,
no worker, `CHIP_EN=0`, and `RDY=0`.

### Batch dispatch and trigger ownership

`runBatch()` selects the compile-time mode and cycle count. Single mode runs
one initialize/iteration/shutdown sequence. Persistent mode initializes once,
keeps resources between iterations, checks the `Ready` boundary, and shuts
down once at the end. Cold mode initializes and shuts down around every
iteration and stops at the first failed iteration.

`batchActive_` is set before batch work begins. Triggers during an active batch
are rejected and stale trigger flags are cleared before and after execution.

Each batch emits a start record, one verbose bounded cycle record per attempt,
and a final aggregate record. `DebugService::kMaxLogMessageLen` is 200 bytes;
the static queue and transmit buffer use that capacity.

## 5. Verified bench results

### Single-cycle baseline

The normal lifecycle associated successfully, obtained host DHCP, disconnected,
removed netifs, deinitialized the module, and reached `CHIP_EN=0`/`RDY=0`.
The representative measurements were 39,080 B initial free heap, 21,896 B
minimum-ever heap, 33,720 B post-cycle heap, 720 B service stack margin, and
632 B DebugService margin.

### Persistent stress

The three-cycle smoke run passed `3/3`. The later R4 run passed `100/100`:

- W6X, Wi-Fi, and host LwIP initialized once.
- All cycles passed DHCP, disconnect callback, link-down, and IPv4 clearing.
- Live netifs and the worker remained present between iterations.
- Final teardown produced zero pbufs, removed netifs, a stopped worker, and
  `CHIP_EN=0`/`RDY=0`.
- Free heap stabilized at 23,984 B after each cycle; minimum-ever heap was
  21,552 B.
- Active task count remained 11 and returned to 8 after teardown.
- Service margin was at least 872 B; DebugService margin was at least 656 B.
- Debug drops, RX overflow, and RX truncation were zero.

This closes the persistent functional and resource run.

### Cold restart

The latest 20-cycle run passed `20/20` functionally. Every cycle completed
connect, DHCP, disconnect, netif teardown, Wi-Fi/W6X teardown, and final GPIO
checks. Pbufs were zero, workers stopped, netifs were removed, and task count
returned to 8 after teardown. RX overflow and truncation were zero; the 11
pre-existing `busyDrop` events did not increase.

The run did not pass the resource-stability gate. Post-cycle free heap fell
from 33,896 B after cycle 1 to 27,864 B after cycle 20, and minimum-ever heap
fell to 15,744 B. This confirms the full-shutdown resource-loss problem
requires further investigation.

### Failure paths completed

Missing credentials failed before W6X initialization with
`credentials-unavailable`; heap stayed at 39,080 B and task count at 7.

Incorrect password failed at bounded `connect` with status `2 (ERROR)` and
preserved `connect/2` through cleanup. Cleanup reached zero pbufs, removed
netifs, stopped the worker, and final `CHIP_EN=0`/`RDY=0`.

AP unavailable failed at bounded `connect` with status `2 (ERROR)` after about
3.6 seconds. Cleanup completed and a later retry succeeded after the AP
returned. Five pre-existing USB `busyDrop` events did not increase.

## 6. Remaining work and decisions

### Full-shutdown resource investigation

Repeat a controlled full-shutdown run while recording heap immediately before
initialization, after each initializer, after disconnect, after each
deinitializer, and after settling. Correlate every change with the existing
LwIP, Wi-Fi, SPI, command-handler, modem, pbuf, and task counters. The goal is
to identify whether the loss is in LwIP teardown, Wi-Fi teardown, W6X/vendor
teardown, or allocator reuse/fragmentation.

Do not select full shutdown as the production daily policy until this gate is
resolved. Persistent initialization with automatic standby is the fallback.

### Cold-restart baseline and electrical checks

Establish task-count baselines before startup, after LwIP core initialization,
between cold iterations, and after final teardown. The persistent LwIP core
may explain the overall `7 -> 8` boundary, but cold resource stability still
requires proof.

Use a logic analyzer and current measurement to verify CS deassertion before
CHIP_EN low, RDY idle level, shutdown current, and absence of GPIO back-power.

### Deferred failure paths

The remaining difficult R5 cases are intentionally deferred for now. They are
open acceptance items, not passed tests. Revisit them if a later product or
teardown change makes controlled reproduction practical.

## 7. Validation commands

The GNU Arm tools are already on `PATH`. Use the existing Cube CMake executable
to build both Debug variants:

```bash
"$CUBE_CMAKE" --build build/Debug-HostController
"$CUBE_CMAKE" --build build/Debug-DisplayController
```

The latest builds completed successfully. `git diff --check` also passes.

## 8. Exit criteria

Phase 3 can be closed when the following are documented:

- R1 lifecycle and first-failure results remain authoritative.
- R2 teardown heap stability is explained or accepted by an explicit product
  decision.
- Persistent 100-cycle operation remains stable.
- Required failure-path coverage is either completed or formally waived.
- Cold-restart resource stability is proven, or full shutdown is rejected as
  the production policy with the fallback documented.
- Electrical shutdown and power behavior are verified.
- Both Debug variants build without tracked credentials or secret logs.

Until then, keep HTTP/TLS and daily scheduling as later-phase work.
