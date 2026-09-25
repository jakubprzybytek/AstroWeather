# Common Firmware Code

Code shared by the two AstroWeather firmware images:
[HostControllerA](../HostControllerA/README.md), the Wi-Fi host on an
STM32G0B1, and [DisplayController](../DisplayController/README.md), the remote
display boards on an STM32G070.

## What Is Here

| Directory | Contents |
| --- | --- |
| `Src/Display`, `Inc/Display` | Logical display content (`DisplayTypes`), the PCB segment and matrix encoding (`DisplayCodec`), the multiplexing refresh task (`PcbDisplayBoard`), the I2C message format (`DisplayI2cProtocol`), the address straps (`DisplayAddress`) and the `DisplayBoard` interface |
| `Src/Device`, `Inc/Device` | The `SCT2xxx` LED driver chain over SPI |
| `Src/Utils`, `Inc/Utils` | `Task`/`TaskBase` (CMSIS-RTOS2 tasks with static stacks and a registry), `Mutex`, `Led`, `SwitchInput`, `Crc32` |
| `Src/Debug`, `Inc/Debug` | `BlinkingLed` |
| `tests/` | Native unit tests for the above, and the pieces both projects' own tests reuse: the HAL and RTOS stubs (`stubs/`), `Expect.hpp` (`support/`), `add_native_test()` (`NativeTest.cmake`) and a stand-in `main.h` (`board/`) |

The display code is described in
[HostControllerA/docs/Display.md](../HostControllerA/docs/Display.md), the
tasks and mutexes in
[Architecture.md](../HostControllerA/docs/Architecture.md#shared-code).

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
workflow `firmware-native-tests.yml` runs these suites and the host's on every
push. How to write a test, the stubs and the coverage table are in
[HostControllerA/docs/Testing.md](../HostControllerA/docs/Testing.md).
