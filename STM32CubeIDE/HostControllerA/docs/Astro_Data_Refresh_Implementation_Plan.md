# Astro Data Refresh and Main Loop Refactor Plan

## 1. Goal

Refactor the HostController so that:

- GPIO switch interrupts are translated by a shared `Utils/SwitchInput` adapter,
  without a dedicated switch task.
- `MainLoopTask` is a responsive event dispatcher for switches and future
  application events.
- Fetching, validating, parsing, and publishing astro data is one clearly named
  operation owned by a separate `AstroDataRefreshTask`.
- A refresh never blocks `MainLoopTask`; switch 2 and future events remain
  responsive throughout network, parsing, and display work.
- Only one refresh may be pending or active. Later requests are ignored and
  logged with their trigger source.
- A refresh can be requested by switch 1 or the USB CDC command
  `astro refresh`.
- Shared switch input remains reusable by the DisplayController variant.

The term **refresh** is used for the complete application transaction:

```text
fetch response -> validate response -> parse astro data -> publish display
```

`AstroDataRefreshTask` is preferred over names tied only to HTTP because the
task owns the complete user-visible update, not just transport.

## 2. Current Constraints

- `MainLoopTask` currently calls `FetchSt67Data()` synchronously. That API waits
  for `St67HttpFetchTask` to complete, so it must not run in the future main
  event loop.
- `SwitchTask` owns a 1536-byte stack only to wait for two flags, blink an LED,
  and invoke callbacks.
- `ConsoleService` already parses commands in task context, so it can request a
  refresh directly without changing the USB CDC interrupt bridge.
- `Display::submit()` serializes hardware submission. Setters reached through
  `local()` and `remote()` are intentionally unsynchronized: the astro refresh,
  console, and current-sense clients may overwrite one another's pending data.
  The resulting behavior is last-writer-wins, and a submitted frame may contain
  a mix of updates from different clients.
- `CurrentSenseTask` currently writes local numeric display 0. Astro display
  mapping may use the same field; whichever client updates it last determines
  its next displayed value.
- `Display::submit()` currently has no result and remote-board submission is
  disabled. Those limitations must be resolved before a refresh can report
  authoritative multi-board publication success.
- The API returns six display blocks. `Display` currently contains one local
  board and four remote boards, so a fifth remote board must be added before
  the complete response can be published.

The authoritative wire contract is maintained in
`../../../sst/docs/api.md` and `../../../sst/docs/api-payload.md`. Protocol
version 1 is a bounded ASCII `key=value` format, not JSON.

No peripheral, pin, or CubeMX configuration change is required for this
refactor.

## 3. Target Architecture

```text
HAL GPIO EXTI callback
        |
        v
Utils::SwitchInput ----------------------+
                                         |
USB CDC -> ConsoleService                v
              |                    MainLoopTask
              |                    | switch 1
              |                    | switch 2
              |                    | future events
              |                    +----------------------+
              |                                           |
              +---- astro refresh ------------------------v
                                                   AstroDataRefreshTask
                                                   1. fetch
                                                   2. validate
                                                   3. parse
                                                   4. publish
                                                          |
                                                          v
                                                        Display
```

`St67HttpFetchTask` remains the sole network lifecycle owner.
`AstroDataRefreshTask` may wait for its existing synchronous client API because
only the refresh worker is blocked; `MainLoopTask` and `ConsoleService` remain
runnable. Converting the ST67 client handoff to a completion callback or flag is
optional follow-up work, not a prerequisite for main-loop responsiveness.

## 4. Proposed File Layout

```text
User/Inc/Utils/SwitchInput.hpp
User/Src/Utils/SwitchInput.cpp

User/Inc/HostController/MainLoopTask.hpp
User/Src/HostController/MainLoopTask.cpp

User/Inc/HostController/AstroData.hpp
User/Inc/HostController/AstroDataParser.hpp
User/Src/HostController/AstroDataParser.cpp
User/Inc/HostController/AstroDataRefreshTask.hpp
User/Src/HostController/AstroDataRefreshTask.cpp

User/Inc/Console/AstroCommand.hpp
User/Src/Console/AstroCommand.cpp
```

Remove `User/Inc/SwitchTask.hpp` and `User/Src/SwitchTask.cpp` after all callers
have migrated. The existing CMake source glob will include the new shared and
HostController-specific files automatically.

## 5. Component Contracts

### 5.1 `Utils::SwitchInput`

`SwitchInput` is an interrupt adapter, not a task. It owns the existing
`HAL_GPIO_EXTI_Falling_Callback` forwarding point and maps generated GPIO pins
to caller-provided thread flags.

Suggested interface:

```cpp
namespace Utils {

class SwitchInput {
public:
    static SwitchInput& instance();

    void attach(osThreadId_t recipient,
                uint32_t switch1Flag,
                uint32_t switch2Flag);
    void detach();
    static void handleExtiFalling(uint16_t gpioPin);
};

} // namespace Utils
```

Requirements:

- `handleExtiFalling()` performs only pin comparison and
  `osThreadFlagsSet()`; it must not log, blink, delay, or invoke application
  workflows in interrupt context.
- An interrupt received before `attach()` or after `detach()` is safely
  ignored.
- The adapter does not include HostController or DisplayController headers.
- Each variant chooses its recipient and event bits during `AppVariant_Init()`.
- Keep the HAL callback implementation in `SwitchInput.cpp`, under `User`, so
  generated interrupt sources remain untouched.

### 5.2 `MainLoopTask`

`MainLoopTask` becomes the only HostController switch-event consumer. Its
event bits should be named by meaning:

```cpp
static constexpr uint32_t kEventSwitch1 = 1U << 0;
static constexpr uint32_t kEventSwitch2 = 1U << 1;
```

Responsibilities:

- Wait for all application event flags with `osFlagsWaitAny`.
- Log switch presses and request the appropriate LED blink in task context.
- On switch 1, call
  `AstroDataRefreshTask::requestRefresh(RefreshTrigger::Switch1)`.
- On switch 2, call `HostController::TriggerSt67ConnectivityCycle()`.
- Process every set bit in a returned flag word; do not use an `else if` chain.
- Remain free of HTTP buffers, CRC logic, parsing logic, and display mapping.

The LED used for switch feedback should move into HostController variant
composition and be injected into `MainLoopTask`. DisplayController may inject
its own LED policy when it attaches `SwitchInput` to a future variant main
loop.

### 5.3 `AstroDataRefreshTask`

Suggested public contract:

```cpp
namespace HostController {

enum class RefreshTrigger : uint8_t {
    Switch1,
    Console,
    Scheduled,
};

enum class RefreshRequestResult : uint8_t {
    Accepted,
    Busy,
    Unavailable,
};

class AstroDataRefreshTask : public Task<2048> {
public:
    static AstroDataRefreshTask& instance();
    void init(Display::Display* display);
    RefreshRequestResult requestRefresh(RefreshTrigger trigger);
};

} // namespace HostController
```

The worker should expose clearly separated private stages:

```cpp
void executeRefresh(RefreshTrigger trigger);
bool fetchPayload();
bool validatePayload() const;
ParseResult parsePayload(AstroData& output) const;
bool publishDisplay(const AstroData& data);
```

Requirements:

- The response buffer and fetch request move from `MainLoopTask` into this task.
- Initially preserve the current fetch result, CRC validation, and summary logs.
  Payload preview logging may remain temporarily until parsing is integrated.
- `active_` becomes true before the run flag is posted and remains true through
  fetch, validation, parsing, and display submission.
- Test-and-set and clear operations for `active_` must be race-free across
  `MainLoopTask` and `ConsoleService`. Use a very short FreeRTOS critical
  section around the state transition; no refresh trigger is called from an
  ISR.
- If posting the worker flag fails, restore the inactive state and report
  `Unavailable`.
- Every exit path clears `active_`, including fetch, CRC, parse, and display
  failures.
- A request received while pending or active returns `Busy`, starts no second
  run, and emits one warning such as:

  ```text
  AstroDataRefresh trigger ignored: active source=console
  ```

- Start and completion logs include trigger source and stage-specific status.
- Fetch, integrity, or parse failure leaves the current display state intact.
  A hardware submission failure may update the local board and some remotes
  before a later remote fails; log the failed board and continue submitting the
  remaining boards.

The refresh task starts at 2048 bytes because raw and per-field diagnostic
logging substantially increases caller stack use. Tune it only from measured
high-water marks after hardware logging is stable.

### 5.4 Astro domain model and parser

Keep transport bytes separate from application data:

- `AstroData` contains exactly six validated board models, corresponding to
  `display=0` through `display=5`.
- `AstroDataParser` is pure C++ without HAL or RTOS dependencies.
- Parsing writes into a temporary `AstroData` and publishes it only after the
  entire payload validates.
- Parse the version 1 ASCII `key=value` protocol incrementally by lines. Accept
  LF and CRLF, split known records at the first `=`, and do not allocate.
- Require `protocol=1`, six consecutive display indexes, the supported
  `board=num4x4_matrix5x21` value, every required record in contract order, and
  a complete final display block.
- Recognize `configurationId` as a required header record but ignore its value.
  Do not compare it with the requested identifier. Bound its value to 20 ASCII
  characters so the parser can use a fixed line buffer.
- Recognize each `nightId` record in the required position but do not parse or
  validate its date value.
- Ignore unknown keys as required for forward compatibility. Unknown records do
  not replace or satisfy required records.
- Parse available times from the contract's `HH:MM` form into hour and minute
  components, then pass them to `NumericDisplay::setTime()`. Do not duplicate
  display range or representability rules in the parser; `NumericDisplay`
  decides whether the parsed value can be shown and uses its normal error
  pattern when it cannot.
- Parse available temperatures from the contract's signed decimal form, then
  pass the value to `NumericDisplay::setValue(value, 1U)`. Reject malformed or
  unconvertible text, but leave range, rounding, and representability decisions
  to `NumericDisplay`.
- Validate each matrix as exactly 21 characters from `*`, `.`, and `?`, or the
  single unavailable-row sentinel `?`.
- Define explicit outcomes for malformed lines, unsupported protocol or board,
  missing/out-of-order records, invalid display indexes, invalid time,
  invalid temperature, invalid matrix, response truncation, and trailing
  incomplete data.

Version 1 maps each API display block directly to one physical board:

```text
display=0 -> local board
display=1 -> remote board 0
display=2 -> remote board 1
display=3 -> remote board 2
display=4 -> remote board 3
display=5 -> remote board 4
```

The numeric records map directly to the four firmware numeric indexes:

```text
numeric_0 -> numeric(0): sunset
numeric_1 -> numeric(1): sunrise
numeric_2 -> numeric(2): maximum temperature
numeric_3 -> numeric(3): minimum temperature
```

There is no `numeric_4` in protocol version 1.

Map `matrix_0` through `matrix_3` directly to matrix rows 0 through 3. Clear
matrix row 4 because it is reserved and absent in version 1. Map both a whole
row `?` and individual `?` slots to LEDs off; the matrix has no separate error
indication.

For numeric `?` values, render a distinct unavailable pattern consisting of
the decimal-point segment on all four visible digits. Use the existing five-slot
raw segment interface:

```cpp
NumericSegments unavailable{{kSegmentDp, kSegmentDp, kSegmentDp,
                             kSegmentDp, 0U}};
numeric.setSegments(unavailable);
```

The fifth slot remains zero because it controls the extra indicators. Expose the
normalized decimal-point mask through the display interface instead of
duplicating its value in the astro mapping. The parser/domain model must retain
whether a numeric value was available so publication can choose between the
ordinary typed setter and this segment pattern. A protocol `?` is valid
unavailable data, not a parser failure. Malformed or unconvertible numerical
text still rejects the payload.

Add native fixture tests for valid payloads, each required-field failure,
time and temperature boundaries, six-board ordering, missing-data rendering,
oversized input, and unchanged output on failure.

### 5.5 Display submission policy

`Display::submit()` continues to serialize SPI and I2C transfer sequences with
its existing mutex. It must be the only synchronization boundary for display
clients.

The astro refresh, console, and current-sense clients may update any pending
logical field through the existing setters without locking. They may overwrite
one another's values, and a single submitted frame may combine fields written
by different clients. This is intentional last-writer-wins behavior: console
and current sensing are diagnostic clients, while astro refresh is the normal
source of display content.

Do not add a transaction API, setter mutex, display-field reservations, or
ownership checks for this change. A later product requirement for all-or-
nothing display frames would be the reason to revisit that policy.

Change display submission to return a result once remote transmission is
enabled. The refresh remains active until local and remote submission finishes,
and its final log distinguishes parse success from publication failure.

Extend `Display` from four to five remote board pointers. Add the fifth static
`BufferedDisplayBoard` in HostController composition at address `0x14`, after
the existing `0x10` through `0x13` boards. Preserve stable submission order and
continue submitting later boards if an earlier remote transfer fails.

### 5.6 Console command

Add a focused `Console::handleAstroCommand()` following the existing ADC and
display command pattern. The first command is exact and argument-free:

```text
astro refresh
```

Responses:

```text
OK astro-refresh=started
ERR astro-refresh-busy
ERR astro-refresh-unavailable
ERR invalid-argument
```

Add this line to `help`:

```text
OK 'astro refresh' - fetch and publish astro data, example: 'astro refresh'
```

The command handler calls
`requestRefresh(RefreshTrigger::Console)` and returns immediately. It does not
wait for completion. Detailed completion or failure remains asynchronous in
normal logs.

Move `CommandResult` into a neutral `Console/CommandResult.hpp` while adding
the new handler, instead of making ADC and astro commands depend on
`DisplayCommand.hpp`. Extend it with `Busy` and `Unavailable` only if that
keeps dispatch handling uniform; otherwise use a command-specific result enum.

## 6. Startup and Lifetime

Update HostController `AppVariant_Init()` in this order:

1. Initialize and start logging.
2. Construct or initialize display users.
3. Start `St67HttpFetchTask`.
4. Initialize and start `AstroDataRefreshTask` with `Display`.
5. Initialize and start `MainLoopTask` with the switch-feedback LED.
6. Attach `Utils::SwitchInput` to the valid main-loop thread handle.
7. Initialize and start `ConsoleService`.

The exact order of unrelated display/current-sense services may remain as it is,
but no caller may request a refresh before the refresh worker has a valid task
handle and display pointer.

Remove the unconditional `SwitchTask` object and `switchTask.start()` from
`AstroWeather.cpp`. Shared startup must not select variant-specific switch
behavior.

For the current DisplayController variant, leave `SwitchInput` unattached; its
interrupts are safely ignored. When that variant gains a main loop, it can
attach the same adapter without pulling in HostController behavior.

## 7. Implementation Sequence

### Phase 1: Introduce the refresh worker

1. Rename/extract the current `MainLoopTask` fetch body into
   `AstroDataRefreshTask`.
2. Preserve response buffering, fetch result reporting, CRC validation, and
   temporary response summary behavior.
3. Implement the race-free pending/active guard and trigger-source logging.
4. Add a minimal event-only `MainLoopTask` that triggers the worker.

Exit criteria:

- A refresh produces the same fetch result as before.
- While it runs, `MainLoopTask` continues processing synthetic event flags.
- Repeated requests from different tasks produce one run and busy warnings.

### Phase 2: Replace `SwitchTask`

1. Add `Utils/SwitchInput` and move the HAL EXTI callback into it.
2. Route switch flags directly to `MainLoopTask`.
3. Move switch logging and LED feedback into main-loop event handlers.
4. Remove callback registration, `SwitchTask`, and its startup call.

Exit criteria:

- Both switches preserve their current behavior.
- Switch 2 is handled while an astro refresh is active.
- The DisplayController image links with shared `SwitchInput` unattached.
- Runtime task diagnostics no longer list `SwitchTask`.

### Phase 3: Add the console trigger

1. Add `AstroCommand` and dispatch it before the final invalid-command path.
2. Add `astro refresh` to help output and serial communication documentation.
3. Return immediate accepted, busy, unavailable, or invalid responses.

Exit criteria:

- Switch and console triggers enter the same `requestRefresh()` API.
- Sending repeated commands during a run never schedules a second refresh.
- Console RX and unrelated commands remain responsive during a refresh.

### Phase 4: Add parsing and display publication

1. Treat `sst/docs/api.md` and `sst/docs/api-payload.md` as the version 1
  endpoint contract and create `AstroData` for six boards.
2. Implement and native-test the bounded line-oriented `AstroDataParser`.
3. Expose the normalized decimal-point mask and test the four-dot unavailable
  pattern through the existing five-slot `setSegments()` API.
4. Extend `Display` and HostController composition to one local plus five remote
  boards.
5. Implement astro-to-display mapping using the existing setter APIs.
6. Call `Display::submit()` after mapping the validated model.
7. Publish only a fully validated model and retain old content after a parser or
  transport failure.

Exit criteria:

- Parser fixtures pass without firmware dependencies.
- All six payload blocks map to the local board and five remote boards in order.
- Numeric `?` values render four decimal points; matrix `?` values render off.
- Concurrent producers may overwrite pending fields; `Display::submit()` still
  prevents overlapping SPI/I2C transfer sequences.
- One successful refresh updates its intended local and remote fields, subject
  to the documented last-writer-wins policy.
- Fetch, validation, parse, and publication failures are distinguishable in
  logs. Fetch, validation, and parse failures do not modify display state;
  publication failures report any partially submitted board set.

### Phase 5: Harden and document

1. Remove temporary payload printing once structured parse diagnostics exist.
2. Measure stack high-water marks and static RAM after task changes.
3. Update `Display.md`, `Serial_COM_Communication.md`, and the ST67 Phase 4/5
   status documents with the implemented ownership and command behavior.
4. Add a future scheduler through
   `requestRefresh(RefreshTrigger::Scheduled)` without creating another path.

## 8. Validation Matrix

### Native tests

- Valid and invalid astro payload fixtures.
- Exact protocol record ordering, missing records, unsupported protocol/board,
  truncated body, and unknown-key handling.
- Time parsing and forwarding to `setTime()`, including values that the display
  accepts or converts to its normal error pattern.
- Temperature parsing and forwarding to `setValue(value, 1U)`, including values
  that the display converts to its normal error pattern.
- `configurationId` values up to 20 characters and ignored `nightId` values.
- Numerical and matrix unavailable-value rendering.
- Parser boundary values and unchanged output on failure.
- Exact `astro refresh` command matching and rejection of extra arguments.
- Astro-to-display mapping against expected logical board state.
- Display transaction tests where practical with fake boards.

### Firmware builds

- Build Debug HostController.
- Build Release HostController.
- Build Debug DisplayController to verify shared `SwitchInput` has no
  HostController dependency.
- Run all existing native tests plus new parser/command tests.

### Target checks

1. Press switch 1 once and observe one complete refresh.
2. Press switch 1 repeatedly during fetch, parse, and display stages; observe
   ignored-trigger warnings and no second run.
3. Send `astro refresh` during an active switch-triggered run; observe
   `ERR astro-refresh-busy`.
4. Start from the console and press switch 1; observe the same busy behavior.
5. Press switch 2 during network fetch and verify its action starts promptly.
6. Run `help`, `status`, ADC, and manual display commands during refresh.
7. Inject fetch, CRC, and malformed-payload failures; verify the worker returns
  to idle and the previous display state remains intact. Inject a remote
  display-submit failure and verify it is reported without preventing later
  remote submissions.
8. Verify task count drops by one after removing `SwitchTask` and record stack
   and minimum-ever heap margins.
9. Verify one local and five remote board updates, including I2C address `0x14`.

## 9. Resource Expectation

Before the refactor, `SwitchTask` and `MainLoopTask` reserve 3072 bytes of task
stack in total. A reasonable initial target is:

- `MainLoopTask`: 1024 bytes.
- `AstroDataRefreshTask`: 2048 bytes.
- `SwitchInput`: no task stack or control block.

This reduces those task stacks by 512 bytes while adding the dedicated workflow
boundary. Final sizes must follow measured stack watermarks, especially after
the parser and display mapping are implemented.

## 10. Completion Criteria

The change is complete when there is one shared switch ISR adapter, one
responsive variant main loop, and one refresh worker owning the complete astro
data transaction. Switch 1, `astro refresh`, and future scheduling all use the
same guarded request API; duplicate requests never queue another run; switch 2
remains responsive; and `Display::submit()` serializes hardware transfers while
clients retain intentional last-writer-wins access to pending display state.