# DisplayController Firmware

Firmware for the AstroWeather remote display boards: an STM32G070CBT6 that
will receive a display buffer from the host over I2C and multiplex it onto its
own four seven-segment displays and 5x21 dot matrix. A display board is the
same PCB as the host, populated without the Wi-Fi module, the USB port and the
EEPROM; see
[Display_Board_Purchasing.md](../HostControllerA/docs/Display_Board_Purchasing.md).

## Features

Started 2026-09-25. The firmware implements the features below, but no display
board has been built yet: everything marked 🔵 compiles and its logic is unit
tested, but it has not run on hardware.

The display behaviour and the I2C link are documented once, for both boards, in
the host's [Display.md](../HostControllerA/docs/Display.md); how this firmware
is put together is in [docs/Architecture.md](docs/Architecture.md).

Status: ✅ done · 🔵 built, not yet run on a board · 🔴 not started.

| Feature | Status | Notes | Document |
| --- | --- | --- | --- |
| **Build and platform** | | | |
| Separate CubeMX project on the STM32G070, `Debug` and `Release` presets | ✅ Done | Compiles the shared code from `../Common` | [Build and Flash](#build-and-flash) |
| FreeRTOS with statically allocated tasks | ✅ Done | Heap 3072 B; only `defaultTask` uses it | [Architecture.md](docs/Architecture.md#tasks) |
| Remove `defaultTask` | 🔴 Not started | CubeMX's placeholder task only idles. Delete it in CubeMX (FreeRTOS, Tasks and Queues), then shrink `configTOTAL_HEAP_SIZE`, since nothing else uses the heap; frees about 3 KB of RAM | [Architecture.md](docs/Architecture.md#tasks) |
| **Display** | | | |
| Drive the local LED board (5-slot multiplex, 250 Hz) | 🔵 Built | `PcbDisplayBoard` on SPI1 and TIM6 | [Display.md](../HostControllerA/docs/Display.md#refresh-operation) |
| Safe power-up: outputs blanked, all slots off | ✅ Done | `SCT_ENABLE` and the slot selects start high (CubeMX) | [Display.md](../HostControllerA/docs/Display.md#refresh-operation) |
| Boot self-test: every segment, indicator and dot for 1 s | 🔵 Built | Shows dead segments without the host | [Architecture.md](docs/Architecture.md#screens) |
| Board address at boot: `Ad12` for 0x12, for 2 s | 🔵 Built | Checks the straps in place | [Architecture.md](docs/Architecture.md#screens) |
| "No data" state: segment G on the last digit of every numeric display, matrix blank | 🔵 Built | Shared (`Display::noDataState()`); after the boot screens until the first frame. The host shows it too (verified on the host board) | [Display.md](../HostControllerA/docs/Display.md#no-data) |
| Back to "no data" after 7 h without a frame | 🔵 Built | Just over the host's 6-hour refresh interval, since the host sends only on a refresh or a `display` command | [Display.md](../HostControllerA/docs/Display.md#no-data) |
| **I2C link to the host** | | | |
| Address from the `ADDR_0..2` straps (27 IDs, `0x10`–`0x2A`) | 🔵 Built | `detectBoardAddress()`; the pins are left analog afterwards | [Display.md](../HostControllerA/docs/Display.md#i2c-transport) |
| I2C target: receive the 36-byte message, command `0x01` | 🔵 Built | Interrupt listen mode; the message is decoded in the `DisplayApp` task | [Architecture.md](docs/Architecture.md#i2c-target) |
| Listen only once the address is known | 🔵 Built | I2C1 is re-initialised with the strap address before listening starts | [Architecture.md](docs/Architecture.md#i2c-target) |
| Reject unknown commands and short writes, keeping the previous frame | 🔵 Built | Counted in `g_displayStats` | [Architecture.md](docs/Architecture.md#i2c-target) |
| Recover from bus errors | 🔵 Built | Listening is restarted after an error, and checked every second | [Architecture.md](docs/Architecture.md#i2c-target) |
| **Brightness** | | | |
| Never drive the bussed `LOW_POWER_ENABLE` line | ✅ Done | PB8 is analog ([Hardware review](../../KiCad/Hardware_Review.md) M-4) | [Display.md](../HostControllerA/docs/Display.md#low-brightness) |
| **Development aids** | | | |
| Heartbeat on `LED_1` | ✅ Done | 20 ms on every 2 s | [Architecture.md](docs/Architecture.md#tasks) |
| `LED_2` flashes on each accepted frame | 🔵 Built | 20 ms | [Architecture.md](docs/Architecture.md#screens) |
| Switch 1 steps through test screens, switch 2 shows the address | 🔵 Built | All segments, then an identify pattern, then back; test screens close after 60 s, the address after 3 s | [Architecture.md](docs/Architecture.md#screens) |
| Diagnostic counters | 🔵 Built | `g_displayStats`, read over SWD: frames accepted and rejected, short writes, probes, bus errors, listen restarts, stale timeouts | [Architecture.md](docs/Architecture.md#diagnostics) |
| Stack overflow hook, as on the host | 🔵 Built | Halts with the task name in `g_stackOverflowTaskName` | [Architecture.md](docs/Architecture.md#diagnostics) |
| Console over USART2 (PA2/PA3, 115200) | 🔴 Not started | The G070 has no USB; USART2 is disabled in CubeMX for now. Wiring in [Display_Board_Purchasing.md](../HostControllerA/docs/Display_Board_Purchasing.md#console-over-uart) | |
| **Tests** | | | |
| Native tests for the shared code (codec, protocol, address, "no data") | ✅ Done | 5 suites in `../Common/tests` | [Testing.md](../HostControllerA/docs/Testing.md) |
| Native tests for the screens and the stale-data timeout | ✅ Done | 2 suites in `tests/` | [Build and Flash](#build-and-flash) |

## Hardware

From `DisplayController.ioc`; the pins and their labels match the host so the
shared code compiles unchanged.

| Peripheral | Pins | Use |
| --- | --- | --- |
| SPI1, master TX only, 1 Mbit/s | `PB3` SCK, `PB5` MOSI | SCT2xxx daisy chain |
| GPIO | `PB6` `SCT_LATCH`, `PB7` `SCT_ENABLE` (starts high: outputs blanked) | SCT latch and output enable |
| GPIO | `PD3`, `PA15`, `PD1`, `PD2`, `PD0` = `DISPLAY_1_EN`..`DISPLAY_5_EN` (start high: off) | Active-low multiplex selects |
| TIM6, 16 MHz / 16000 / 4 | none | 250 Hz multiplex tick, interrupt (the G070 has no TIM2) |
| I2C1, target, interrupt | `PA9` SCL, `PA10` SDA | Messages from the host, at `0x10` + the strap ID |
| GPIO inputs, pull-down at reset | `PB10`, `PB11`, `PB14` = `ADDR_0`..`ADDR_2` | Board address straps; analog once read |
| GPIO EXTI, falling edge | `PB12` `SWITCH_1`, `PB13` `SWITCH_2` | Switches, for `Utils::SwitchInput` |
| GPIO | `PC13` `LED_1`, `PB9` `LED_2` | Heartbeat, frame received |
| Analog | `PB8` `LOW_POWER_ENABLE` | Bussed net driven by the host; never driven here ([Hardware review](../../KiCad/Hardware_Review.md) M-4) |
| SWD | `PA13`, `PA14` | Debug |
| TIM1 | none | HAL time base |

FreeRTOS through CMSIS-RTOS2, 3072-byte heap: the tasks and mutexes use
static memory, so only `defaultTask` comes from it.

## Build and Flash

Same toolchain and workflow as the host
([Development.md](../HostControllerA/docs/Development.md)): GNU Arm Embedded
with Ninja through the CMake presets `Debug` and `Release`.

```bash
cmake --preset Debug
cmake --build --preset Debug
arm-none-eabi-size build/Debug/DisplayController.elf
STM32_Programmer_CLI -c port=SWD -w build/Debug/DisplayController.elf -v -hardRst
```

`.vscode/launch.json` has **DisplayController Debug** and **Release** entries
for the ST-LINK debugger.

Native tests for this project's own logic (the screens and the stale-data
timeout) build with the host compiler; the shared code's suites are in
`../Common/tests`:

```bash
cmake --preset NativeTests
cmake --build --preset NativeTests
ctest --test-dir build/native-tests-local --output-on-failure
```

## Layout

| Path | Owner |
| --- | --- |
| `DisplayController.ioc`, `Core/`, `Drivers/`, `Middlewares/`, `cmake/stm32cubemx/`, the startup file and linker script | CubeMX; application changes only inside `USER CODE` sections |
| `CMakeLists.txt`, `CMakePresets.json` | The project: adds `User/` and `../Common` to the CubeMX target |
| `User/Inc`, `User/Src` | Application code specific to the display board; see [docs/Architecture.md](docs/Architecture.md) |
| `tests/` | Native tests of that code |
| `../Common` | Code shared with the host, compiled into this image |
