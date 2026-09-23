# ST67W611M1 Phase 2 Official Driver Smoke-Test Plan

## 1. Objective

Replace the manual raw-SPI probe runtime path with the smallest official
ST67W6X T02 driver workflow:

```text
button press
  -> W6X_Init
  -> W6X_RegisterAppCb
  -> W6X_WiFi_Init
  -> MX_LWIP_Init
  -> W6X_WiFi_Scan
  -> bounded wait for scan callback
  -> structured result and resource logs
  -> remain initialized
```

This phase proves that the generated SPI/DMA transport, ST driver tasks, Wi-Fi
control path, and host LwIP netif can run together from a cold
`ST67_CHIP_EN`-low start. It does not connect to an access point, run DHCP,
fetch data, or repeatedly tear down and recreate the networking stack.

## 2. Source-backed decisions

### 2.1 Initialization order

Use the following order for the vendored X-CUBE-ST67W61 1.3.0 sources:

1. `W6X_Init()`
2. `W6X_RegisterAppCb()`
3. `W6X_WiFi_Init()`
4. `MX_LWIP_Init()`
5. `W6X_WiFi_Scan()`

This order supersedes the earlier wording that placed callback registration
before `W6X_Init()`. The local `w6x_api.h` documents `W6X_Init()` as the first
W6X call, followed by callback registration. `W6X_WiFi_Init()` then verifies
that `APP_wifi_cb` is non-null. LwIP must follow Wi-Fi initialization because
`MX_LWIP_Init()` reads the station and AP MAC addresses and calls
`W6X_Netif_Init()`.

### 2.2 Single transport owner

The official service task becomes the only application owner of SPI1,
`ST67_CS`, `ST67_RDY`, `ST67_CHIP_EN`, and `ST67_BOOT`. Do not start
`St67ProbeTask` in the same image. Keep its source available for diagnosis,
but leave it unreachable from normal `AppVariant_Init()` startup.

### 2.3 One initialization attempt per boot

The worker accepts one smoke-test trigger per host boot. After successful
initialization and scanning it remains initialized. After a partial failure it
reports the failed stage and does not permit an in-place retry unless all
resources created up to that stage have a proven cleanup path.

This restriction is deliberate. `MX_LWIP_Init()` allocates two netifs, starts
the TCP/IP task, and starts the ST netif task, but the generated T02 code has no
symmetric public deinitializer. Reinitialization belongs to Phase 3.

## 3. Application boundary

Add an application-owned worker with this public surface:

```cpp
namespace HostController {

void StartSt67ServiceTask();
void TriggerSt67SmokeTest();

}  // namespace HostController
```

Implement it as the existing static task pattern: a private singleton derived
from `Task<StackSizeBytes>`, one trigger flag, and a blocking `run()` loop. The
initial stack allocation should be at least the current manual probe's 2048
bytes and must be tuned from the measured high-water mark, not reduced by
inspection.

The task owns these persistent members:

- lifecycle state: `Off`, `Starting`, `Ready`, `Scanning`, `Complete`, or
  `Fault`;
- a zero-initialized `W6X_App_Cb_t` callback table;
- scan-complete and driver-error thread flags;
- scan status and AP count copied by the scan callback;
- the most recent `W6X_Status_t` and failed-stage identifier;
- a guard that rejects duplicate triggers after initialization begins.

Do not retain pointers supplied to W6X callbacks. The driver's scan result
points into driver-owned storage. Copy only bounded scalar data needed by the
worker, such as status and AP count; format AP records synchronously in the
scan callback only if logging is proven safe, or preferably copy a bounded
summary and log it from the worker task.

## 4. Callback design

Create static C-compatible callback functions in the service implementation
and forward them to the singleton without exposing the C++ object to the C
driver.

Configure the persistent callback table as follows:

| Callback | Phase 2 behavior |
|---|---|
| `APP_wifi_cb` | Record unexpected Wi-Fi events and set a worker flag; do not run lifecycle logic in the callback. |
| `APP_error_cb` | Copy the W6X status, record a stable error indication, and set the driver-error flag. |
| `APP_net_cb` | Null; T02 data-plane events are not consumed by this smoke test. |
| `APP_mqtt_cb` | Null. |
| `APP_ble_cb` | Null. |

Pass a separate scan-result callback to `W6X_WiFi_Scan()`. It must:

1. tolerate a null result pointer when status reports failure;
2. clamp `entry->Count` before any iteration or copy;
3. copy scan status and count into task-owned fields;
4. set the scan-complete thread flag;
5. return immediately without delays, W6X calls, or full workflow logic.

The worker waits for either scan completion or driver error with a configured
timeout. A timeout is a failed smoke test even if the command-start return was
`W6X_STATUS_OK`.

## 5. Configuration

Populate `Appli/App/app_config.h` only with non-secret Phase 2 settings:

```c
#define APP_ST67_STARTUP_DELAY_MS       4000U
#define APP_ST67_SCAN_TIMEOUT_MS       15000U
#define APP_ST67_SCAN_MAX_RESULTS      20U
```

Use an active, all-channel, unfiltered scan:

- zero-fill `W6X_WiFi_Scan_Opts_t` first;
- leave `SSID`, `MAC`, and `Channel` zero for no filter;
- select the driver's active-scan enum value;
- set `MaxCnt` to `APP_ST67_SCAN_MAX_RESULTS`, matching the vendored driver's
   fixed 20-entry scan-result storage.

Do not add SSIDs, passwords, URLs, certificates, or other secrets in this
phase. Keep the existing driver defaults of autoconnect disabled and automatic
power save enabled.

## 6. Implementation sequence

### Increment 2.1 - Compile the official API boundary

1. Add `User/Inc/HostController/St67ServiceTask.hpp` with the two public
   functions.
2. Add `User/Src/HostController/St67ServiceTask.cpp` with the singleton task,
   persistent callback table, and callback forwarding functions.
3. Add the non-secret timeout and scan limits to `app_config.h`.
4. Initially implement only trigger acceptance and compile-time construction
   of the W6X callback and scan option types.
5. Build `Debug-HostController` with warnings treated according to the current
   project policy.

Acceptance check: C and C++ headers interoperate without casts that discard
type safety, and the new source is picked up by the existing variant source
glob.

### Increment 2.2 - Replace manual runtime ownership

1. In `User/Src/HostController/AppVariant.cpp`, start `St67ServiceTask`
   instead of `St67ProbeTask`.
2. Bind switch 1 to `TriggerSt67SmokeTest()`.
3. Leave switch 2 unbound or bind it to a harmless status report; do not retain
   a raw AT trigger while the official driver can own the transport.
4. Confirm that no normal startup path calls `StartSt67ProbeTask()`.

Acceptance check: a symbol/reference search finds one active ST67 task starter
in the HostController variant, and it is the official service task.

### Increment 2.3 - Driver and Wi-Fi initialization

On the first trigger, execute each call exactly once and stop at the first
failure:

1. Record starting heap and task stack watermarks.
2. Set state to `Starting` and call `W6X_Init()`.
3. Read `W6X_GetModuleInfo()` only after successful initialization. Reject a
   null pointer and log module ID plus SDK version components.
4. Require SDK 2.0.106 for the first bench acceptance run. If a different T02
   version is intentionally evaluated, log it and record that decision in the
   master plan rather than silently accepting it.
5. Fill the persistent callback table and call `W6X_RegisterAppCb()`.
6. Call `W6X_WiFi_Init()`.
7. Set state to `Ready` only after all three calls succeed.

Log one concise line per stage with elapsed milliseconds and numeric plus text
W6X status. Do not depend only on ST's internal logging macros, because their
current sink may differ from `DebugService`.

Acceptance check: a cold boot reaches `Ready`, reports T02-compatible module
information, and does not enter `Error_Handler()` or trigger the FreeRTOS stack
overflow hook.

### Increment 2.4 - Host LwIP initialization

1. Call `MX_LWIP_Init()` once, after `W6X_WiFi_Init()`.
2. Treat any nonzero return as a terminal Phase 2 failure.
3. Log heap before and after the call so the two netif allocations and task
   creation cost are visible.
4. Verify that the station and AP netif pointers returned by
   `netif_get_interface()` are non-null.

Do not call `MX_LWIP_Init()` again after any partial success. Its current
failure paths can also leave allocations or tasks behind; capture the exact
failed stage for later teardown work instead of retrying.

Acceptance check: LwIP initialization returns zero, both netifs exist, and the
system remains responsive over USB CDC before a Wi-Fi association exists.

### Increment 2.5 - Bounded scan

1. Clear stale worker flags and task-owned scan results.
2. Set state to `Scanning`.
3. Call `W6X_WiFi_Scan()` with the configured options and result callback.
4. If command start fails, report immediately without waiting.
5. Wait up to `APP_ST67_SCAN_TIMEOUT_MS` for scan complete or driver error.
6. On completion, validate callback status, log AP count, and print a bounded
   summary if AP details were copied safely.
7. Set state to `Complete` and remain initialized.

Zero APs with a successful scan status is a successful transport smoke test
and must be distinguished from timeout, command error, or callback error.

Acceptance check: the callback arrives within the timeout and at least one
bench run reports nearby APs. Repeat the host cold-boot test enough times to
exercise both nonempty and, if encountered, valid empty results.

## 7. Failure behavior

Use stable stage identifiers in every final result:

```text
w6x-init
module-info
callback-register
wifi-init
lwip-init
scan-start
scan-wait
driver-callback
```

For failures before successful `MX_LWIP_Init()`, perform only cleanup already
provided symmetrically by the package and proven for that stage. At minimum,
`W6X_WiFi_DeInit()` may follow a completed Wi-Fi initialization and
`W6X_DeInit()` may follow a completed W6X initialization. Do not improvise
partial LwIP cleanup in Phase 2.

For failures during or after LwIP initialization, record `Fault`, leave the
system available for diagnostics, reject further smoke-test triggers, and
require a host reset before another attempt. This avoids hiding leaks or
duplicate task creation behind an apparent retry.

## 8. Observability and resource gates

Emit these records through `DebugService`:

- smoke-test run ID and state transition;
- stage, return status, and elapsed time;
- module model and SDK version;
- scan command status, callback status, AP count, and elapsed time;
- `xPortGetFreeHeapSize()` and `xPortGetMinimumEverFreeHeapSize()` before
  initialization, after W6X, after Wi-Fi, after LwIP, and after scan;
- service task stack high-water mark after every stage;
- final `complete` or `fault` record with the stable stage identifier.

Keep log messages below `DebugService`'s 128-byte record limit and avoid a
burst containing every AP field. The debug task previously retained only 120
bytes of stack, so check its high-water mark during driver banner output and
scan logging.

Phase 2 resource acceptance gates:

- minimum-ever free heap remains at least 8 KB;
- every observed task retains a nonzero stack margin, with at least 256 bytes
  retained by the new service task and `DebugService` during this milestone;
- no downward heap trend appears across repeated cold-host-boot smoke tests;
- no debug queue overflow obscures the final stage result.

## 9. Verification matrix

### Build checks

1. Build `Debug-HostController` from a cleanly regenerated CMake graph.
2. Confirm there are no undefined W6X, LwIP, callback, or C/C++ linkage
   symbols.
3. Record FLASH, RAM, `.data`, and `.bss` deltas from the Phase 1 baseline.
4. Confirm `Debug-DisplayController` still builds if shared headers were
   touched.

### Bench checks

1. Cold host boot with `CHIP_EN` initially low; press switch 1 once.
2. Verify approximately 1 MHz SPI mode-0 traffic and DMA completion on a logic
   analyzer.
3. Confirm module info identifies T02 SDK 2.0.106.
4. Confirm W6X, Wi-Fi, LwIP, and scan stages complete in order.
5. Confirm a second switch press is rejected without creating tasks,
   allocating memory, or issuing another scan.
6. Repeat at least 10 cold-host-boot smoke tests before declaring Phase 2
   stable; retain heap and stack minima from each run.
7. Run with no visible AP or RF shielding if practical and verify that a valid
   zero-result scan is not misreported as a transport timeout.

### Failure-injection checks

1. Hold the module disabled or disconnect it and verify bounded `w6x-init`
   failure reporting.
2. Use an incompatible T01 image only if a recovery/programming path is ready;
   verify the network-mode mismatch is reported.
3. Temporarily shorten the scan timeout and verify a bounded `scan-wait`
   failure without a retry or duplicate initialization.

## 10. Expected file changes

| File | Change |
|---|---|
| `Appli/App/app_config.h` | Add Phase 2 startup, scan timeout, and scan count constants. |
| `User/Inc/HostController/St67ServiceTask.hpp` | Declare task start and smoke-test trigger API. |
| `User/Src/HostController/St67ServiceTask.cpp` | Own callbacks, lifecycle state, official initialization, LwIP init, scan, and diagnostics. |
| `User/Src/HostController/AppVariant.cpp` | Replace manual probe startup and switch handlers. |
| `User/Src/HostController/St67ProbeTask.cpp` | No functional change required; leave compiled but unreachable, or exclude it in a later cleanup. |
| `docs/ST67_Daily_Fetch_Implementation_Plan.md` | Link this detailed Phase 2 plan and record bench results after execution. |

No changes are expected in generated STM32 peripheral files, the SPI target
port, LwIP internals, driver core sources, credentials, HTTP, or TLS for this
phase. Any required change in those areas is a reason to stop, document the
observed blocker, and reassess scope.

## 11. Definition of done

Phase 2 is complete when all of the following are true:

- the manual probe is not started in normal HostController runtime;
- one button press from a cold host boot completes `W6X_Init()`, callback
  registration, `W6X_WiFi_Init()`, `MX_LWIP_Init()`, and a bounded scan;
- the module reports the accepted T02 firmware version;
- the scan callback reports a valid completion, including valid zero-result
  handling;
- duplicate triggers do not repeat initialization or allocate resources;
- ten cold-host-boot runs complete without heap decline, stack overflow, DMA
  error, or transport timeout;
- resource logs meet the Phase 2 gates and the final implementation results
  are appended to the master plan.
