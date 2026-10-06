# Common Firmware Code

Code shared by the two AstroWeather firmware images:
[HostControllerA](../HostControllerA/README.md), the Wi-Fi host on an
STM32G0B1, and [DisplayController](../DisplayController/README.md), the remote
display boards on an STM32G070.

## What Is Here

| Directory | Contents |
| --- | --- |
| `Src/Display`, `Inc/Display` | Logical display content and attributes (`DisplayTypes`), the PCB segment and matrix encoding (`DisplayCodec`), the pass sequencing (`RefreshSequencer`), the interrupt-driven multiplexing (`PcbDisplayBoard`), the refresh timeline and its sync (`Timeline`, `TimelineServo`, `TimelineSync`), the boot screens (`BootScreens`), the I2C message format (`DisplayI2cProtocol`), the address straps (`DisplayAddress`) and the `DisplayBoard` interface |
| `Src/Device`, `Inc/Device` | The `SCT2xxx` LED driver chain over SPI; `HsiTrim`, the HSI16 user trim |
| `Src/Utils`, `Inc/Utils` | `Task`/`TaskBase` (CMSIS-RTOS2 tasks with static stacks and a registry), `Mutex`, `SwitchInput`, `Crc32`, `microsNow()` (`MicroClock`) |
| `Src/Debug`, `Inc/Debug` | `PulseLed` and `activityLed()` (`LED_2`, also from interrupts and C through `ActivityLedBridge.h`); the `LED_1` heartbeat comes from the refresh interrupt (`PcbDisplayBoard`) |
| `tests/` | Native unit tests for the above, and the pieces both projects' own tests reuse: the HAL and RTOS stubs (`stubs/`), `Expect.hpp` (`support/`), `add_native_test()` (`NativeTest.cmake`) and a stand-in `main.h` (`board/`) |

Each part is documented in the shared firmware docs, [firmware/Docs](../Docs/README.md):

| Part | Document |
| --- | --- |
| Display content, attributes, PCB encoding, the multiplexing refresh, the timeline sync and the heartbeat, boot screens, `SCT2xxx` | [Display.md](../Docs/Display.md) |
| The I2C message format, the sync broadcast and status read, the address straps | [I2C.md](../Docs/I2C.md) |
| `Task`, `Mutex`, `SwitchInput`, `Crc32`, `microsNow()`, `HsiTrim`, the LEDs | [Utilities.md](../Docs/Utilities.md) |
| The native test kit and the suites in `tests/` | [Testing.md](../Docs/Testing.md) |

## How It Is Built

This is not a library. Each firmware project includes `CommonSources.cmake`
and compiles `${COMMON_SOURCES}` itself, against its own HAL, device define
(`STM32G0B1xx` or `STM32G070xx`), `stm32g0xx_hal_conf.h`, `FreeRTOSConfig.h`
and `main.h`. A change here must therefore be built in both projects.

Rules for code in this tree:

- It may include `main.h`, `cmsis_os2.h` and `FreeRTOS.h`, and may use the pin
  labels both CubeMX projects define: `LED_1`, `LED_2`, `SWITCH_1`, `SWITCH_2`,
  `ADDR_0`..`ADDR_2`, `DISPLAY_1_EN`..`DISPLAY_5_EN`, `SCT_LATCH`, `SCT_ENABLE`.
  Peripheral handles (`hspi3` on the host, `hspi1` on the display board, the
  refresh timer) are passed in by the project, never named here.
- It must not depend on anything only one project has: the host's `LogService`,
  USB, the settings store, the EEPROM, Wi-Fi.

## Tests

The native suites build with the host compiler and run under CTest, without a
board (MSYS2 UCRT64 on the development workstation, see the presets):

```bash
cmake --preset NativeTests
cmake --build --preset NativeTests
ctest --test-dir build/native-tests-local --output-on-failure
```

`NativeTests-Coverage` adds `--coverage` for `gcovr`. The GitHub Actions
workflow `firmware-native-tests.yml` runs these suites and both projects' on
every push. How to write a test, the stubs and what each suite covers are in
[Testing.md](../Docs/Testing.md).
