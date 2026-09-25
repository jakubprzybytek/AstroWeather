# Testing

## Overview

The firmware logic is tested by native unit tests: small C++17 executables
built with the host compiler and run by CTest, without a board. Code that
needs hardware is tested on the bench (see [Bench Tests](#bench-tests)).

The rule that keeps most code testable is to keep decisions out of the tasks:
parsing, mapping and timing logic lives in small units that take plain values
and return plain values, and the FreeRTOS task only reads the hardware, calls
the unit and acts on the result. Where a unit still needs the HAL, the RTOS or
the log, the test links a stand-in instead of the real thing.

## Running the Tests

Build and run all suites with the `NativeTests` preset (MSYS2 UCRT64 compiler,
see [Development.md](Development.md#native-tests)):

```bash
cmake --preset NativeTests
cmake --build --preset NativeTests
ctest --test-dir build/native-tests-local --output-on-failure
```

The suites for the code shared with the DisplayController are a separate CMake
project with the same presets: run the same three commands from `../Common`.
The DisplayController's own suites (its screens and stale-data timeout) run the
same way from `../DisplayController`.

### Coverage

The `NativeTests-Coverage` preset builds the same suites with `--coverage`
into `build/native-tests-coverage`. After running them, `gcovr`
(`python -m pip install gcovr`) prints line coverage for `User/`:

```bash
cmake --preset NativeTests-Coverage
cmake --build --preset NativeTests-Coverage
ctest --test-dir build/native-tests-coverage
python -m gcovr -r . --filter 'User/' \
    --gcov-executable /c/Progs/msys64/ucrt64/bin/gcov.exe \
    build/native-tests-coverage --txt
```

The report lists only files that some suite compiles, so a file that no test
links does not show up as 0%. The [coverage table](#coverage-by-module) below
tracks those.

### Continuous Integration

`.github/workflows/firmware-native-tests.yml` builds and runs the suites of
`firmware/Common`, `firmware/HostControllerA` and `firmware/DisplayController`
with coverage on Ubuntu for every push and pull request that touches any of
them, and writes the
`gcovr` summaries to the job summary. It configures CMake directly because the
presets carry Windows toolchain paths.

## Writing a Test

Each suite is one `tests/<Name>Tests.cpp` with a `main()`, registered in
`tests/CMakeLists.txt` (or, for shared code, in `../Common/tests/CMakeLists.txt`):

```cmake
add_native_test(<name>
    SOURCES <Name>Tests.cpp
    SUT User/Src/<path to the code under test>.cpp
    [STUBS]
    [FAKE_LOG]
)
```

- `SUT` paths are relative to the project root; shared sources are reached as
  `../Common/Src/...`. `../Common/Inc`, `User/Inc` and `../Common/tests/support`
  are always on the include path.
- `STUBS` adds `../Common/tests/stubs` and `Core/Inc`, so the real `main.h`
  compiles against a stand-in HAL, and links the stub implementations. The
  shared code's own suites compile against `../Common/tests/board/main.h`, a
  stand-in with the pin labels both boards define.
- `FAKE_LOG` links the recording `LogService` fake from `tests/fakes` (and
  implies `STUBS`).

`add_native_test()` is defined once, in `../Common/tests/NativeTest.cmake`; each
`tests/CMakeLists.txt` sets the project root, include and `main.h` directories
before including it.

### Assertions

`../Common/tests/support/Expect.hpp` provides:

| Helper | Use |
| --- | --- |
| `Test::expect(condition, "case")` | Boolean check |
| `Test::expectEqual(actual, expected, "case")` | Equality; prints both values on failure (enums and bytes as numbers) |
| `Test::fail("case")` | Record a failure from a custom helper |
| `Test::finish("suite")` | Return from `main()`: exit code 1 if anything failed |

A failed check is counted and the suite carries on, so one run reports every
failing case.

### Stubs

`../Common/tests/stubs/` holds stand-ins for `stm32g0xx_hal.h`, `cmsis_os2.h`,
`FreeRTOS.h`, `queue.h` and `task.h`. Nothing is scheduled: threads are never
created and mutexes always succeed. `StubHal.hpp` steers them from a test:

| Function | Effect |
| --- | --- |
| `Stub::reset()` | Clock to 0, all pins floating inputs |
| `Stub::setTick(ms)`, `Stub::advanceTick(ms)` | Fake clock behind `HAL_GetTick()` and `osKernelGetTickCount()`; `osDelay()` and `HAL_Delay()` advance it |
| `Stub::setStrap(port, pin, Strap::Low / High / Floating)` | What an input pin reads; a floating pin follows the pull set by `HAL_GPIO_Init()` |
| `Stub::outputState(port, pin)`, `Stub::configuredPull(port, pin)` | Inspect what the code did to a pin |

Add HAL declarations to the stub as code under test needs them, with the same
names and values as the real HAL.

### Log Fake

`tests/fakes/FakeLogService.cpp` implements the `LogService` API and records
each `log()`, `logf()` and `sendLine()` call synchronously, without the uptime
prefix, the queue or USB. `FakeLog::lines()`, `FakeLog::contains()`,
`FakeLog::clear()` and `FakeLog::dump()` read it back. This is what lets
console commands and task code link in a test.

## Coverage by Module

Line coverage from the `NativeTests-Coverage` preset on 2026-09-23: 95% over
the 24 files the suites compile (983 lines). Since 2026-09-25 the
`numeric_display_tests`, `display_codec_tests`, `display_i2c_protocol_tests`,
`display_address_tests` and `crc32_tests` suites live in `../Common/tests`; the
rest in `tests/`.

| Module | Suite | Line coverage |
| --- | --- | --- |
| Test infrastructure (stubs, log fake) | `test_support_tests` | — |
| `NumericDisplay` (`DisplayTypes`) | `numeric_display_tests` | 97% |
| `DisplayCodec` | `display_codec_tests` | 100% |
| `DisplayI2cProtocol` | `display_i2c_protocol_tests` | 100% |
| `DisplayAddress` | `display_address_tests` | 100% |
| `CurrentSenseConversion` | `current_sense_conversion_tests` | 100% |
| `CalendarDate` | `calendar_date_tests` | 100% |
| `RtcTrim` | `rtc_trim_tests` | 100% |
| `ClockSync` | `clock_sync_tests` | 100% |
| `RefreshSchedule` | `refresh_schedule_tests` | 95% |
| `AstroDataParser` | `astro_data_parser_tests` | 96% |
| `AstroDisplayMapper` | `astro_display_mapper_tests` | 100% |
| `AstroProgressBar` | `astro_progress_bar_tests` | 98% |
| `Crc32` | `crc32_tests` | 100% |
| `SettingsCodec` | `settings_codec_tests` | 89% |
| `HttpResponseParser` | `http_response_parser_tests` | 100% |
| `St67HttpRules` | `st67_http_rules_tests` | 100% |
| `St67ConnectDiagnosis` | `st67_connect_diagnosis_tests` | 100% |
| `St67FetchStatusMap` | `st67_fetch_status_map_tests` | 100% |
| `SettingsStore`, `Eeprom24AA04` | — | Phase 3 |
| `BufferedDisplayBoard` | — | Phase 3 |
| Console commands, `ConsoleService` line assembly | — | Phase 4 |
| `AstroDataRefreshTask` run loop, `HttpClient` socket loop | — | Only through the extracted units |
| `LogService`, `PcbDisplayBoard`, `ClockTask` RTC access, Wi-Fi session | — | Bench only |

## Plan

The plan was drawn up on 2026-09-23 from the documentation review.

| Phase | State |
| --- | --- |
| 0 Infrastructure | Done 2026-09-23 |
| 1 Tests without production changes | Done 2026-09-23 |
| 2 Extract pure logic | Done 2026-09-23 |
| 3 Device fakes | Not started |
| 4 Console commands | Not started |

### Phase 0: Infrastructure (done)

Delivered as described in [Writing a Test](#writing-a-test): `Expect.hpp` and
`add_native_test()`, the HAL/RTOS stubs with a fake clock and GPIO model, the
recording `LogService` fake, the `NativeTests-Coverage` preset with `gcovr`,
and the GitHub Actions job. `test_support_tests` checks the stubs and the fake
themselves. All suites that existed before were moved onto `Expect.hpp`, so a
run reports every failing case instead of stopping at the first.

### Phase 1: Tests Without Production Changes (done)

| Suite | What it pins |
| --- | --- |
| `display_i2c_protocol_tests` | Serialize/deserialize round trip, 36-byte message, command `0x01`; null, wrong-size and wrong-command messages rejected without touching the destination; bits 21-23 masked |
| `display_address_tests` | All 27 strap combinations (high = 2, floating = 1, low = 0), pins left analog without pull, `0x10 + id` and 0 for id 27 or more |
| `display_codec_tests` | Golden vectors from the wiring tables in [Display.md](Display.md): every segment of every digit, L1-L3, the 21 matrix columns, the matrix row order (bottom row first) |
| `astro_data_parser_tests` | 95-character line limit, CRLF, blocks out of order or missing, truncated payload, `configurationId` of 20 and 21 characters, `protocol=2`, `?` rows and numerics |
| `numeric_display_tests` | The fixed-point rules after the fix below |

The one production change: `NumericDisplay::setFixed()` dropped the zero before
the decimal point, so -0.5 showed as `-5`. The tests were written first
(15 checks failed), then the fix: the digit left of the decimal point is always
shown, so -0.5 shows as `-0.5` and 0.5 as `0.5`. A value that then needs more
than four positions shows the error pattern; that includes every negative
value at precision 3.

### Phase 2: Extract Pure Logic (done)

Behaviour-preserving refactors; each task now calls the extracted unit:

| Extracted from | Unit | Suite |
| --- | --- | --- |
| `HttpClient.cpp` | `HttpResponseParser`: header end, status line, `Content-Length`, the 2048-byte header and 4096-byte body limits | `http_response_parser_tests` |
| `St67HttpFetcher.cpp` | `St67HttpRules`: host/path validation, `Content-Type` check | `st67_http_rules_tests` |
| `St67NetworkSession.cpp` | `St67ConnectDiagnosis`: reason code → result, scan fallback, failure messages | `st67_connect_diagnosis_tests` |
| `St67HttpFetchTask.cpp` | `St67FetchStatusMap`: failure stage → `St67FetchStatus` | `st67_fetch_status_map_tests` |
| `AstroDataRefreshTask.cpp` | `AstroDisplayMapper`: 6 blocks → local and remote boards | `astro_display_mapper_tests` |
| `AstroDataRefreshTask.cpp` | `AstroProgressBar`: row-4 pattern and its blink/hold timing | `astro_progress_bar_tests` |
| `AstroDataRefreshTask.cpp`, `St67HttpFetcher.cpp` | `Crc32` (header-only, shared by both) | `crc32_tests` |
| `CurrentSenseTask.cpp` | `CurrentSenseConversion`: VDDA from VREFINT, temperature, display value | `current_sense_conversion_tests` |

One deliberate behaviour change: a current reading above 9999 mA now always
shows the error pattern; before, a reading above 32767 mA wrapped to a wrong
value. RAM is unchanged; the Debug image grew by about 1.5 KB of flash.

Still reached only through the extracted units: the `AstroDataRefreshTask` run
loop and the `HttpClient` socket loop.

### Phase 3: Device Fakes

- `IEeprom` (or a template parameter) and a `FakeEeprom` that counts page
  writes: `SettingsStore` writes only changed pages, falls back to defaults,
  and reports a read failure as `ReadFailed` rather than `Blank`.
- `FakeI2cBus`: `Eeprom24AA04` page splitting and write-cycle polling timeout;
  `BufferedDisplayBoard` online/offline logging with the fake clock.

### Phase 4: Console Commands

- Extract `ConsoleService` line assembly: CR/LF, the 127-character limit,
  empty lines, the 8-deep queue.
- Per command group, input line → expected `OK`/`ERR` lines, using
  [Console.md](Console.md) as the specification.
- Fix the `help wifi` claim that `wifi set` echoes the password.
- Make `display set` validate with the display's own fit rule; it still uses
  the range from before the phase 1 fix, so some values it accepts show the
  error pattern.

### Follow-ups Found in Phases 1-2

- Move `WifiConnectResult` (`St67HttpFetchTask.hpp`) and `St67FetchStatus`
  (`St67FetchTypes.hpp`) into plain headers without `cmsis_os2.h`, so
  `st67_connect_diagnosis_tests`, `st67_fetch_status_map_tests` and
  `astro_progress_bar_tests` no longer need `STUBS`.
- Decide on the pinned behaviour listed under
  [Known Issues Pinned by Tests](#known-issues-pinned-by-tests), in particular
  case-insensitive HTTP header names and reporting an oversized
  `Content-Length` as `ResponseTooLarge`.
- Remove the unreachable `Truncated` check in `AstroDataParser.cpp`.

### Bench Tests

Not worth unit testing: the real Wi-Fi session, `LogService` over USB,
`PcbDisplayBoard` multiplexing timing and RTC register access. A scripted
smoke test on `tools/astro_console.py capture` (boot, `status`,
`astro refresh`, check the success lines) and the ADC known-voltage test from
the [hardware review](../../../KiCad/Hardware_Review.md) cover them.

### Targets

- After phase 2 (met): every pure-logic unit has a suite, and the known bugs
  have tests; 95% line coverage of the code the suites compile.
- After phase 4: 80% line coverage of `User/Src`, excluding the bench-only
  files, enforced in CI.

## Known Issues Pinned by Tests

Behaviour the tests record as it is today, to be decided separately. Each
test that pins one says "current behaviour".

**HTTP response** (`http_response_parser_tests`, `st67_http_rules_tests`):

- Header names are matched case-sensitively (`Content-Length`,
  `Content-Type`), although HTTP header names are case-insensitive.
- Header lines must end in CRLF; bare-LF headers never terminate.
- Chunked `Transfer-Encoding` is not decoded: chunk markers are passed on as
  body, and only the server closing the connection ends it.
- `Content-Length` accepts a leading `+`, rejects a trailing space, and takes
  the last of duplicate headers.
- Body bytes that arrive in the same read as the blank line count against the
  2048-byte header buffer.
- The `Content-Type` check is a prefix search anywhere in the headers: extra
  parameters pass, case and spacing must match exactly, and a header such as
  `X-Content-Type:` can match first.
- Fixed 2026-09-24: the host check now rejects whitespace and `/`, and the path
  check whitespace.

**Fetch status** (`st67_fetch_status_map_tests`):

- Failures at module-info, callback-register, lwip-init, lwip-netif,
  disconnect and other stages all report `HttpFailure`.
- A `Content-Length` over 4096 reports `HttpFailure`, not `ResponseTooLarge`:
  the response is refused before the body callback that sets the flag.
- `CleanupFailure` cannot happen for a client fetch.

**Wi-Fi diagnosis** (`st67_connect_diagnosis_tests`): reason code 0 on a
failed connect classifies as `Failed`.

**Payload parser** (`astro_data_parser_tests`): a payload cut in the middle of
a line parses that partial line as complete, so the error is the record's own
(`InvalidMatrix`, `InvalidTemperature`) rather than `Truncated`; the
`Truncated` check for that case in `AstroDataParser.cpp` is unreachable.

**Progress bar** (`astro_progress_bar_tests`): a processing failure blinks the
full bar, the same pattern as success; the blink phase follows the tick and
jumps at the 32-bit wrap.

**Numeric display** (`numeric_display_tests`): every negative value at
precision 3 needs five positions and shows the error pattern. The console's
`display set` still pre-validates with the old range, so such values are
accepted and then shown as the error pattern.
