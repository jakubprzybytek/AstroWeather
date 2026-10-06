# Native Tests

The firmware logic is tested by native unit tests: small C++17 executables
built with the PC's compiler and run by CTest, without a board. Each of the
three projects has its own suites; the test kit they share lives in
`firmware/Common/tests`. Code that needs hardware is tested on the bench (see
the host's [Testing.md](../HostControllerA/Docs/Testing.md#bench-tests)).

The rule that keeps most code testable is to keep decisions out of the tasks:
parsing, mapping and timing logic lives in small units that take plain values
and return plain values, and the FreeRTOS task only reads the hardware, calls
the unit and acts on the result. Where a unit still needs the HAL, the RTOS or
the log, the test links a stand-in instead of the real thing.

## Suites

| Project | Suites | What they cover |
| --- | ---: | --- |
| `Common` | 10 | The shared display code, the timeline sync, the I2C message, the address straps, `Crc32`; listed [below](#common-suites) |
| `HostControllerA` | 16 | Parser, mapper, progress bar, schedule, clock, display sync schedule, settings codec, HTTP and Wi-Fi rules, error log; see the host's [Testing.md](../HostControllerA/Docs/Testing.md#coverage-by-module) |
| `DisplayController` | 3 | `FrameAssembler` (staging of the attribute messages), the screens, the stale-data timeout |

### Common suites

| Suite | Covers |
| --- | --- |
| `numeric_display_tests` | `setFixed()`, including magnitudes below one and values that do not fit, both `setValue()` overloads, `setTime()` with the blank leading zero and the error pattern, `setTimeUnset()`, `setBlank()`, `setSegments()`, `setNoData()` and `noDataState()` |
| `display_attributes_tests` | Attribute defaults, the blink and level setters with their masks, clamping and out-of-range indices, the plane operations keeping bits 21-23 zero |
| `display_codec_tests` | Golden vectors from the wiring tables in [Display.md](Display.md#pcb-encoding): every segment of every digit on its bit, byte and slot, the indicators, the 21 matrix columns and bits 21-23, the order of the matrix rows in the frame |
| `display_passes_tests` | The pass table's invariants (sums to 100, the level percentages, the matrix's 70 %), which elements each pass and blink phase show, and that `encodePasses()` is `encodePcb()` of exactly those |
| `refresh_sequencer_tests` | Pass lengths from the table and their validation, the longest-first order, slot and frame boundaries, `peek()`, the blink phase per frame, the frame adjustment's split over the slots, renumbering the frames |
| `timeline_sync_tests` | `TimelineServo`'s rate and slew, `TimelineSync`'s jump, rate, trend and HSITRIM decisions, `HsiTrim::allowedSteps()`, the heartbeat frame, `positionMicros()`, and a simulated board against the host's sync schedule ([Display.md](Display.md#accuracy)) |
| `display_i2c_protocol_tests` | The 36-byte layout, the round trip, masking of bits 21-23, rejection of short, long and null messages and unknown commands without touching the destination; the sync message |
| `boot_screens_tests` | The slot test and address screens and their timing, shown without disturbing the board's own state |
| `display_address_tests` | All 27 strap combinations through the stub GPIO, the pins left analog without pull, `boardAddress()` limits, `detectBoardAddress()` |
| `crc32_tests` | `Crc32` against known vectors |

Not covered natively: the refresh interrupt itself (frame starts, stamps,
`applySync()`), `Utils::microsNow()`, the HSITRIM register access, the DMA
and SPI transfers, and I2C transfer failures.

## Running the Tests

From the project's directory (`firmware/Common`, `firmware/HostControllerA` or
`firmware/DisplayController`):

```bash
export PATH="/c/Progs/msys64/ucrt64/bin:$PATH"   # MSYS2 UCRT64 compiler and its DLLs
cmake --preset NativeTests
cmake --build --preset NativeTests
ctest --test-dir build/native-tests-local --output-on-failure
```

The presets take the compiler from `C:/Progs/msys64/ucrt64` (see each
`CMakePresets.json`). Its `bin` directory must also be on `PATH`: without it
`cc1plus` cannot load its DLLs, every compile fails with `FAILED: [code=1]` and
no message, and `ctest` quietly runs the old executables.

If `ctest` is unavailable, run a suite directly, for example
`./build/native-tests-local/tests/settings_codec_tests.exe`.

### Coverage

The `NativeTests-Coverage` preset builds the same suites with `--coverage`
into `build/native-tests-coverage`. After running them, `gcovr`
(`python -m pip install gcovr`) prints line coverage, for example for the
host's `User/`:

```bash
cmake --preset NativeTests-Coverage
cmake --build --preset NativeTests-Coverage
ctest --test-dir build/native-tests-coverage
python -m gcovr -r . --filter 'User/' \
    --gcov-executable /c/Progs/msys64/ucrt64/bin/gcov.exe \
    build/native-tests-coverage --txt
```

The report lists only files that some suite compiles, so a file that no test
links does not show up as 0 %.

### Continuous Integration

`.github/workflows/firmware-native-tests.yml` builds and runs the suites of
`firmware/Common`, `firmware/HostControllerA` and `firmware/DisplayController`
with coverage on Ubuntu for every push and pull request that touches any of
them, and writes the `gcovr` summaries to the job summary. It configures CMake
directly because the presets carry Windows toolchain paths.

## Writing a Test

Each suite is one `tests/<Name>Tests.cpp` with a `main()`, registered in the
project's `tests/CMakeLists.txt`:

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
- `STUBS` adds `../Common/tests/stubs` and the project's `Core/Inc`, so the
  real `main.h` compiles against a stand-in HAL, and links the stub
  implementations. Common's own suites compile against
  `Common/tests/board/main.h`, a stand-in with the pin labels both boards
  define.
- `FAKE_LOG` links the host's recording `LogService` fake (see the host's
  [Testing.md](../HostControllerA/Docs/Testing.md#log-fake)) and implies
  `STUBS`.

`add_native_test()` is defined once, in `Common/tests/NativeTest.cmake`; each
`tests/CMakeLists.txt` sets the project root, include and `main.h` directories
before including it.

### Assertions

`Common/tests/support/Expect.hpp` provides:

| Helper | Use |
| --- | --- |
| `Test::expect(condition, "case")` | Boolean check |
| `Test::expectEqual(actual, expected, "case")` | Equality; prints both values on failure (enums and bytes as numbers) |
| `Test::fail("case")` | Record a failure from a custom helper |
| `Test::finish("suite")` | Return from `main()`: exit code 1 if anything failed |

A failed check is counted and the suite carries on, so one run reports every
failing case.

### Stubs

`Common/tests/stubs/` holds stand-ins for `stm32g0xx_hal.h`, `cmsis_os2.h`,
`FreeRTOS.h`, `queue.h` and `task.h`. Nothing is scheduled: threads are never
created and mutexes always succeed. `StubHal.hpp` steers them from a test:

| Function | Effect |
| --- | --- |
| `Stub::reset()` | Clock to 0, all pins floating inputs |
| `Stub::setTick(ms)`, `Stub::advanceTick(ms)` | Fake clock behind `HAL_GetTick()` and `osKernelGetTickCount()`; `osDelay()` and `HAL_Delay()` advance it |
| `Stub::setStrap(port, pin, Strap::Low / High / Floating)` | What an input pin reads; a floating pin follows the pull set by `HAL_GPIO_Init()` |
| `Stub::outputState(port, pin)`, `Stub::configuredPull(port, pin)` | Inspect what the code did to a pin |

Add HAL declarations to the stub as code under test needs them, with the same
names and values as the real HAL. The host's `test_support_tests` suite checks
the stubs and the log fake themselves.
