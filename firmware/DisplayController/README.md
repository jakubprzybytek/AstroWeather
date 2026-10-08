# DisplayController Firmware

Firmware for the AstroWeather remote display boards: an STM32G070CBT6 that
receives its display content from the host over I2C and multiplexes it onto
its own four seven-segment displays and 5x21 dot matrix. A display board is
the same PCB as the host, populated without the Wi-Fi module, the USB port and
the EEPROM; see
[Display_Board_Purchasing.md](../../KiCad/Docs/Display_Board_Purchasing.md).

The first display board runs it on the host's I2C bus at `0x11`.

## Features

Status: ✅ done and checked on the board · 🔵 built and unit tested, not yet
checked on the board · 🔴 not started.

| Feature | Status | Notes | Document |
| --- | --- | --- | --- |
| **Display** | | | |
| Drive the local LED board: five slots, 50 Hz frames, four passes per slot | ✅ | `PcbDisplayBoard` from TIM6's interrupt, SPI1 by DMA; shared with the host | [Display.md](../Docs/Display.md#refresh-operation) |
| Blink and four brightness levels per segment and pixel | ✅ | Sent by the host as attribute planes with the content | [Display.md](../Docs/Display.md#blink-and-brightness-levels) |
| Refresh timeline kept on the host's: blinking and the heartbeat in step on every board | 🔵 | Syncs broadcast by the host on the I2C general call; per-frame rate and phase correction, simulated within a few ms | [TimelineSync.md](../Docs/TimelineSync.md) |
| HSITRIM moved to follow the host's clock | 🔵 | Up to 8 steps from the boot trim, never across an HSICAL band edge; the servo covers the rest, up to 2 % | [TimelineSync.md](../Docs/TimelineSync.md#following-the-host) |
| Safe power-up: outputs blanked, all slots off | ✅ | `SCT_ENABLE` and the slot selects start high (CubeMX); the first frame is prepared before the outputs are enabled | [Architecture.md](Docs/Architecture.md#boot) |
| Boot screens: slot test (each `DISPLAYx_EN` slot on its own, 1 s), then the board address (`Ad11`) and build number, 2 s | ✅ | Shared with the host (`Display::showBootScreens()`) | [Display.md](../Docs/Display.md#boot-screens) |
| "No data" until the first frame, and again after 7 h without one | 🔵 | Shared state (`Display::noDataState()`); 7 h is just over the host's 6-hour refresh interval | [Architecture.md](Docs/Architecture.md#screens) |
| **I2C link to the host** | | | |
| Address from the `ADDR_0..2` straps, `0x10`–`0x2A` | ✅ | Floating 0, ground 1, VCC 2; board 1 reads `0x11` | [I2C.md](../Docs/I2C.md#addresses-and-straps) |
| I2C target: content and attribute messages | ✅ | Interrupt-driven listen; the messages are decoded in the `DisplayApp` task. The I2C interrupt is at priority 1 so every message is acknowledged | [I2C.md](../Docs/I2C.md#display-board-side) |
| Listen only once the address is known | ✅ | Nothing answers on CubeMX's placeholder `0x10` | [I2C.md](../Docs/I2C.md#display-board-side) |
| Timeline sync on the general call, status on a one-byte read | 🔵 | `0xA0` with flags: locked, needs content; a reset board asks for and gets the host's content within a minute | [I2C.md](../Docs/I2C.md#timeline-sync) |
| Reject unknown commands and short writes, keeping the previous frame | 🔵 | Counted in `g_displayStats` | [I2C.md](../Docs/I2C.md#display-board-side) |
| Recover from bus errors | 🔵 | Listening restarts after an error and is checked every second | [I2C.md](../Docs/I2C.md#display-board-side) |
| **Brightness** | | | |
| Never drive the bussed `LOW_POWER_ENABLE` line | ✅ | `PB8` stays analog; the host drives it for every board ([Hardware review](../../KiCad/Docs/Hardware_Review.md) M-4) | [Display.md](../Docs/Display.md#low-brightness) |
| **Development aids** | | | |
| Heartbeat on `LED_1`, 20 ms every 2 s, from the refresh interrupt | 🔵 | Frame 1 of every 100 on the shared timeline, so in step with the host once synced; twice (frames 1 and 11) while not locked to it | [TimelineSync.md](../Docs/TimelineSync.md#the-timeline-on-every-board) |
| `LED_2` flashes on every write addressed to the board | 🔵 | 20 ms, from the address-match interrupt, through the shared `PulseLed`; not for the sync broadcast or the status reads | [Architecture.md](Docs/Architecture.md#screens) |
| Switch 1 steps through test screens, switch 2 shows the address | 🔵 | All segments, then an identify pattern; test screens close after 60 s, the address after 3 s | [Architecture.md](Docs/Architecture.md#screens) |
| Diagnostic counters, read over SWD | ✅ | `g_displayStats`: frames, attributes, rejects, short writes, probes, I2C errors, refresh counters; the sync counters, error, drift, rate and HSITRIM (🔵) | [Architecture.md](Docs/Architecture.md#diagnostics) |
| Stack overflow hook | ✅ | Halts with the task name in `g_stackOverflowTaskName` | [Architecture.md](Docs/Architecture.md#diagnostics) |
| Console over USART2 (`PA2`/`PA3`, 115200) | 🔴 | The G070 has no USB; USART2 is disabled in CubeMX. Wiring in [Display_Board_Purchasing.md](../../KiCad/Docs/Display_Board_Purchasing.md#console-over-uart) | |
| **Platform** | | | |
| CubeMX project on the STM32G070, `Debug` and `Release` presets | ✅ | Compiles the shared code from `../Common` | [Development.md](../Docs/Development.md) |
| FreeRTOS with static tasks; `defaultTask` exits at start | ✅ | The 3072-byte heap is used only by `defaultTask` until it exits | [Architecture.md](Docs/Architecture.md#tasks) |
| Interrupt priorities: I2C 1, everything else 3 | ✅ | | [Architecture.md](Docs/Architecture.md#interrupt-priorities) |
| Native tests for the screens, the stale-data timeout and the frame assembly | ✅ | 3 suites in `tests/`; the shared code's suites, the timeline sync's among them, are in `../Common/tests` | [Testing.md](../Docs/Testing.md) |

## Hardware

From `DisplayController.ioc`; the pins and their labels match the host so the
shared code compiles unchanged.

| Peripheral | Pins | Use |
| --- | --- | --- |
| SPI1, master TX only, 1 Mbit/s, TX by DMA1 channel 1 | `PB3` SCK, `PB5` MOSI | SCT2xxx daisy chain |
| GPIO | `PB6` `SCT_LATCH`, `PB7` `SCT_ENABLE` (starts high: outputs blanked) | SCT latch and output enable |
| GPIO | `PD3`, `PA15`, `PD1`, `PD2`, `PD0` = `DISPLAY_1_EN`..`DISPLAY_5_EN` (start high: off) | Active-low multiplex selects |
| TIM6, prescaler 15 (1 MHz count) | none | Display refresh; the interrupt sets each pass's length (the G070 has no TIM2) |
| I2C1, target, interrupt at priority 1 | `PA9` SCL, `PA10` SDA | Messages from the host, at `0x10` + the strap ID; the timeline sync on the general call (enabled in code, not the `.ioc`) |
| GPIO inputs | `PB10`, `PB11`, `PB14` = `ADDR_0`..`ADDR_2` | Board address straps; analog once read |
| GPIO EXTI, falling edge, internal pull-up | `PB12` `SWITCH_1`, `PB13` `SWITCH_2` | Switches; the buttons and their external pull-ups are optional on a display board |
| GPIO | `PC13` `LED_1`, `PB9` `LED_2` | Heartbeat from the refresh interrupt, I2C writes to the board |
| Analog | `PB8` `LOW_POWER_ENABLE` | Bussed net driven by the host; never driven here |
| SWD | `PA13`, `PA14` | Debug |
| TIM1 | none | HAL time base |

## Build and Flash

The toolchain, presets, flashing and native tests are shared with the other
firmware projects and described in
[Development.md](../Docs/Development.md). In short:

```bash
cmake --preset Debug
cmake --build --preset Debug
STM32_Programmer_CLI -c port=SWD -w build/Debug/DisplayController.elf -v -hardRst
```

## Layout

| Path | Owner |
| --- | --- |
| `DisplayController.ioc`, `Core/`, `Drivers/`, `Middlewares/`, `cmake/stm32cubemx/`, the startup file and linker script | CubeMX; application changes only inside `USER CODE` sections |
| `CMakeLists.txt`, `CMakePresets.json` | The project: adds `User/` and `../Common` to the CubeMX target |
| `User/Inc`, `User/Src` | Application code specific to the display board |
| `tests/` | Native tests of that code |
| `../Common` | Code shared with the host, compiled into this image ([README](../Common/README.md)) |

## Documentation

This project:

- [Docs/Architecture.md](Docs/Architecture.md): boot, tasks, screens, interrupt priorities, the SWD diagnostics
- [Docs/archive](Docs/archive/): [I2C_NACK_Investigation.md](Docs/archive/I2C_NACK_Investigation.md), how the I2C interrupt priority was found

Shared with the host ([firmware/Docs](../Docs/README.md)):

- [Development.md](../Docs/Development.md): build, flash, debug, CubeMX rules
- [Display.md](../Docs/Display.md): the display, its encoding and refresh
- [TimelineSync.md](../Docs/TimelineSync.md): the timeline kept on the host's, the sync schedule and decisions, HSITRIM, measuring it with `tools/stats_log.py`
- [I2C.md](../Docs/I2C.md): the bus, the messages, the addresses, both sides of the link
- [Testing.md](../Docs/Testing.md): native tests
- [Utilities.md](../Docs/Utilities.md): tasks, mutexes, switches, the microsecond clock, HSI trim, LEDs

Hardware: [KiCad/Docs](../../KiCad/Docs/README.md).

## Known Limitations and Open Items

- Not yet checked on the board: rejecting unknown commands and short writes,
  recovery from bus errors, the 7-hour "no data" timeout and the switch test
  screens.
- The timeline sync: a run over a day's temperature, the cause of the host's
  clock wander
  ([TimelineSync.md](../Docs/TimelineSync.md#open-items)).
- No console: the counters are read over SWD until the USART2 console exists.
- The FreeRTOS heap (3072 B) could shrink, since only `defaultTask` uses it
  and only until it exits.
