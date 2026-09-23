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

`.github/workflows/firmware-native-tests.yml` builds and runs the suites with
coverage on Ubuntu for every push and pull request that touches
`firmware/HostControllerA/`, and writes the `gcovr` summary to the job
summary. It configures CMake directly because the presets carry Windows
toolchain paths.

## Writing a Test

Each suite is one `tests/<Name>Tests.cpp` with a `main()`, registered in
`tests/CMakeLists.txt`:

```cmake
add_native_test(<name>
    SOURCES <Name>Tests.cpp
    SUT User/Src/<path to the code under test>.cpp
    [STUBS]
    [FAKE_LOG]
)
```

- `SUT` paths are relative to the project root. `User/Inc` and
  `tests/support` are always on the include path.
- `STUBS` adds `tests/stubs` and `Core/Inc`, so the real `main.h` compiles
  against a stand-in HAL, and links the stub implementations.
- `FAKE_LOG` links the recording `LogService` fake (and implies `STUBS`).

### Assertions

`tests/support/Expect.hpp` provides:

| Helper | Use |
| --- | --- |
| `Test::expect(condition, "case")` | Boolean check |
| `Test::expectEqual(actual, expected, "case")` | Equality; prints both values on failure (enums and bytes as numbers) |
| `Test::fail("case")` | Record a failure from a custom helper |
| `Test::finish("suite")` | Return from `main()`: exit code 1 if anything failed |

A failed check is counted and the suite carries on, so one run reports every
failing case.

### Stubs

`tests/stubs/` holds stand-ins for `stm32g0xx_hal.h`, `cmsis_os2.h`,
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
the 24 files the suites compile (983 lines).

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

### Phase 0: Infrastructure

- Shared `Expect.hpp`, one CTest target per suite, `add_native_test()`.
- HAL/RTOS stubs in `tests/stubs/` with a fake clock and a GPIO model.
- Recording `LogService` fake in `tests/fakes/`.
- `NativeTests-Coverage` preset and `gcovr` report.
- GitHub Actions job running the suites on every push.

### Phase 1: Tests Without Production Changes

| Target | Cases |
| --- | --- |
| `DisplayI2cProtocol` | Serialize/deserialize round trip, 36-byte size, wrong command, short message |
| `DisplayCodec::encodePcb` | Golden vectors from the segment and multiplexing tables in [Display.md](Display.md) |
| `NumericDisplay` | The -0.5 °C → `-5` bug: failing test first, then the fix (fixed) |
| `AstroDataParser` | Line-length limit, CRLF, blocks out of order or missing, truncated payload, `configurationId` length, unsupported protocol, `?` rows |
| `DisplayAddress::detectBoardId` | All tri-state strap combinations through the stub GPIO |

### Phase 2: Extract Pure Logic

Behaviour-preserving refactors that move logic out of the tasks:

| Extract from | Unit |
| --- | --- |
| `HttpClient.cpp` | HTTP response parser (status line, headers, `Content-Length`, limits) |
| `St67HttpFetcher.cpp` | Host/path validation and the `Content-Type` check |
| `St67NetworkSession.cpp` | Wi-Fi connect failure classification and messages |
| `St67HttpFetchTask.cpp` | Failure stage → fetch status mapping |
| `AstroDataRefreshTask.cpp` | Block → board display mapping; progress-bar pattern; CRC-32 |
| `CurrentSenseTask.cpp` | VDDA and temperature arithmetic |

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

### Bench Tests

Not worth unit testing: the real Wi-Fi session, `LogService` over USB,
`PcbDisplayBoard` multiplexing timing and RTC register access. A scripted
smoke test on `tools/astro_console.py capture` (boot, `status`,
`astro refresh`, check the success lines) and the ADC known-voltage test from
the [hardware review](../../../KiCad/Hardware_Review.md) cover them.

### Targets

- After phase 2: every pure-logic unit has a suite, and the known bugs have
  tests.
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
- The host check accepts spaces and `/`; the path check accepts spaces.

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
