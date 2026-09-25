# Firmware Architecture

## Overview

This page describes how the firmware is put together: how it boots, which
tasks run and how they talk to each other, which peripherals and pins it uses,
what the unit tests cover, and which code is present but unused. The feature
documents listed in the [README](../README.md#documentation) describe each
subsystem in depth.

The firmware is C++17 on top of the STM32CubeMX-generated C code for an
STM32G0B1CETx (Cortex-M0+, 16 MHz from HSI16, 512 KB flash, 144 KB RAM),
FreeRTOS through CMSIS-RTOS2, the ST67W6X network driver and LwIP. Application
code lives under `User/` and, for the parts shared with the DisplayController
firmware, under `../Common/` (see [Shared Code](#shared-code)); CubeMX owns
`Core/`, `Drivers/`, `Middlewares/`,
`LWIP/`, `USB_Device/` and `ST67W6X_Network_Driver/`, and application changes
there stay inside `USER CODE` sections (see [CubeMXCompliance.md](CubeMXCompliance.md)).

## Boot Sequence

`main()` in `Core/Src/main.c` runs the generated sequence:

1. `HAL_Init()`, `SystemClock_Config()`.
2. `MX_GPIO_Init()`, `MX_DMA_Init()`, `MX_SPI1_Init()`, `MX_USART2_UART_Init()`,
   `MX_SPI3_Init()`, `MX_I2C1_Init()`, `MX_TIM2_Init()`, `MX_ADC1_Init()`,
   `MX_RTC_Init()`. The `Check_RTC_BKUP` user section returns early from
   `MX_RTC_Init()` when backup register DR0 holds the `TIME` marker, so a reset
   keeps the calendar (see [RTC.md](RTC.md)).
3. `osKernelInitialize()`, then `defaultTask` is created.
4. `AstroWeather_Init()` in `User/Src/AstroWeather.cpp`, from the
   `RTOS_THREADS` user section.
5. `osKernelStart()`.

All of `AstroWeather_Init()` runs before the scheduler starts. The objects it
uses are file-scope statics, constructed before `main()`; their `Mutex` members
create their FreeRTOS mutexes from static storage at that point.

### Init order

`AstroWeather_Init()` first starts the `Led1` `BlinkingLed` task on `LED_1`
(on 20 ms, off 1980 ms: a heartbeat every 2 s), then, in order:

1. `LogService` `init()` (creates the log queue) and `start()`.
2. `settingsStore.load()` reads the EEPROM; the outcome, not the values, is
   logged. This works before the scheduler because EEPROM reads take no
   `osDelay` and the bus mutex is uncontended.
3. `CurrentSenseTask`: logging and display flags from settings, the display,
   then `start()`.
4. `ConsoleService`: `init(&display)`, EEPROM and settings pointers, `start()`.
5. `LowBrightness::set()` applies the saved low brightness to `PB8`, before the
   displays light up.
6. The local board gets the "no data" state (`Display::noDataState()`, see
   [Display.md](Display.md#no-data)), then `localBoard.start()` enables the SCT
   outputs and starts the `DisplayRefresh` task and TIM2.
7. `ClockTask`: display flag and trim from settings (an error is logged if the
   trim is rejected), the display, `start()`.
8. `SetSt67CredentialSource(&settingsStore)`, then `StartSt67HttpFetchTask()`.
   The credential source must be set first, since the fetch task reads the
   credentials on every connect.
9. `AstroDataRefreshTask`: `init(&display)` restores the last successful
   refresh time from backup register DR1 when the RTC is set, then `start()`.
10. `MainLoopTask`: `init(led2, &settingsStore)` (switch 2 saves low
    brightness), `start()`.
11. `SwitchInput::attach()` routes the `SWITCH_1`/`SWITCH_2` EXTI interrupts to
    `MainLoopTask` as thread flags.

`defaultTask` initialises the USB device (`MX_USB_Device_Init()`) once the
scheduler runs and then only sleeps.

## Shared Code

The remote display boards run the separate
[DisplayController](../../DisplayController/README.md) project: an STM32G070
with its own CubeMX configuration, presets and `User/` tree. Code both images
need lives in `../Common` and is compiled into each project against that
project's HAL, `main.h` and FreeRTOS configuration
(`Common/CommonSources.cmake`), so the two CubeMX projects must keep the same
labels for the pins it uses.

| Location | Contents |
| --- | --- |
| `../Common/Src/Display/`, `Inc/Display/` | `DisplayTypes`, `DisplayCodec`, `DisplayI2cProtocol`, `DisplayAddress`, the `DisplayBoard` interface and `PcbDisplayBoard` |
| `../Common/Src/Device/` | `SCT2xxx` |
| `../Common/Src/Utils/`, `Inc/Utils/` | `Task`, `TaskBase`, `Mutex`, `Led`, `SwitchInput`, `Crc32` |
| `../Common/Src/Debug/` | `BlinkingLed` |
| `../Common/tests/` | Their native tests, and the HAL/RTOS stubs, `Expect.hpp` and `add_native_test()` that this project's tests reuse |

Everything else under `User/` is host-only: `Astro/` (the refresh task, parser,
mapper, progress bar and schedule), `Clock/`, `WiFi/`, `Console/`, `Settings/`,
`Sensors/`, `LogService`, the EEPROM driver and `I2cBus`, the aggregate
`Display` with its `BufferedDisplayBoard`s, `LowBrightness` and `MainLoopTask`.
Shared code must not depend on any of these, in particular not on `LogService`.

Every build also regenerates `BuildInfo.cpp` with the build time
(`cmake/BuildInfo.cmake`), reported by the console. There is no version number
or git hash.

## Static Object Graph

The HostController objects are defined at file scope in
`User/Src/AstroWeather.cpp`. `i2c1Bus` is declared first because
objects in one translation unit are constructed in declaration order and the
others hold a reference to it.

| Object | Type | Wraps / owns |
| --- | --- | --- |
| `i2c1Bus` | `Device::I2cBus` | `hi2c1`, with a mutex per transfer |
| `localSct` | `SCT2xxx` | `hspi3`, `SCT_ENABLE`, `SCT_LATCH` |
| `localBoard` | `Display::PcbDisplayBoard` | `localSct`, `htim2`, `DISPLAY_1_EN`..`DISPLAY_5_EN`; is also the `DisplayRefresh` task |
| `remoteBoard1`..`remoteBoard5` | `Display::BufferedDisplayBoard` | `i2c1Bus` at 7-bit addresses `0x10`..`0x14` |
| `display` | `Display::Display` | `localBoard` plus the five remote boards |
| `settingsEeprom` | `Device::Eeprom24AA04` | `i2c1Bus`, address `0x50` |
| `settingsStore` | `Settings::Store` | `settingsEeprom` and the in-RAM `Values` |
| `led2` | `Led` | `LED_2`, blinked by `MainLoopTask` |

The tasks other than `DisplayRefresh` and `Led1` are singletons reached through
`instance()`. The fetch task is private to `User/Src/WiFi/St67HttpFetchTask.cpp`
and is reached through `StartSt67HttpFetchTask()`, `FetchSt67Data()` and
`TriggerSt67ConnectivityCycle()`.

## Tasks

`Task<N>` in `../Common/Inc/Utils/Task.hpp` holds its stack and control block as
members, so application tasks are allocated statically. **`N` is in bytes**,
passed straight to `osThreadNew()` as `stack_size`. `TaskBase` keeps a registry
of up to 16 such tasks, which `stats on` walks to report stack headroom.
Middleware tasks come from the FreeRTOS heap and are not in that registry.

CMSIS-RTOS2 priorities are FreeRTOS priorities (`configMAX_PRIORITIES` is 56):
`osPriorityLow` = 8, `BelowNormal` = 16, `Normal` = 24, `Normal4` = 28,
`Realtime` = 48.

| Task name | Owner | Priority | Stack (bytes) | Allocation | Does |
| --- | --- | --- | ---: | --- | --- |
| `DisplayRefresh` | `Display::PcbDisplayBoard` | Realtime (48) | 1024 | static | Multiplexes the local board, one slot per TIM2 tick |
| `netif` | `LWIP/App/lwip_netif.c` | 50 | 2048 | heap | Passes received frames from the ST67 driver to LwIP |
| `Modem_Process` | ST67 driver `w61_at_common.c` | 47 | 2048 | heap | AT response and event handling |
| `spi_xfer_engine` | ST67 driver `spi_iface.c` | 46 | 1536 | heap | SPI1 transfers to the module |
| `tcpip_thread` | LwIP `tcpip.c` | Normal4 (28) | 4096 | heap | LwIP core |
| `defaultTask` | `Core/Src/main.c` | Normal (24) | 512 | heap | Starts USB, then idles |
| `LogService` | `Debug/LogService.cpp` | Normal (24) | 1536 | static | Drains the log queue to USB CDC; `stats` output |
| `ConsoleService` | `Console/ConsoleService.cpp` | Normal (24) | 2048 | static | Assembles and runs console commands |
| `AstroDataRefresh` | `Astro/AstroDataRefreshTask.cpp` | Normal (24) | 3072 | static | Refresh pipeline, 6-hourly schedule, progress bar |
| `MainLoopTask` | `MainLoopTask.cpp` | Normal (24) | 1536 | static | Switch presses: switch 1 requests a refresh, switch 2 toggles low brightness and saves it |
| `CurrentSense` | `Sensors/CurrentSenseTask.cpp` | BelowNormal (16) | 2048 | static | ADC every 100 ms |
| `Clock` | `Clock/ClockTask.cpp` | BelowNormal (16) | 1024 | static | RTC, `HH:MM` on display 3 |
| `St67HttpFetch` | `WiFi/St67HttpFetchTask.cpp` | BelowNormal (16) | 2560 | static | Owns the ST67 session and the HTTP fetch |
| `Led1` | `Debug/BlinkingLed.cpp` | Low (8) | 768 | static | Heartbeat on `LED_1` |
| `Tmr Svc` | FreeRTOS | 2 | 1024 | static | FreeRTOS timer service |
| `IDLE` | FreeRTOS | 0 | 512 | static | Idle |

The four middleware tasks are created on the first fetch: `Modem_Process` and
`spi_xfer_engine` by `W6X_Init()`, `tcpip_thread` and `netif` by
`MX_LWIP_Init()`. After an ordinary refresh the module and LwIP stay up, so they
persist. The driver priorities 46 and 47 are overridden in
`ST67W6X_Network_Driver/Target/w61_driver_config.h` to keep them below
`DisplayRefresh`; `netif` still runs at 50, above it.

## Inter-task Communication

Tasks wake on CMSIS thread flags almost everywhere; there are two message
queues and five mutexes.

### Thread flags

| Receiver | Set by | Meaning |
| --- | --- | --- |
| `DisplayRefresh` | TIM2 period ISR through `Display_PcbTimerElapsed()` | Refresh the next multiplex slot |
| `MainLoopTask` | `HAL_GPIO_EXTI_Falling_Callback()` via `SwitchInput` | `kEventSwitch1`, `kEventSwitch2` |
| `CurrentSense` | ADC DMA complete / error callbacks | Conversion done or failed |
| `ConsoleService` | USB CDC RX and line-state callbacks | Command queued, host connected |
| `LogService` | `log()` from any task; `setStatsEnabled()` | Log queued, stats changed |
| `Clock` | display and time setters | Redraw |
| `AstroDataRefresh` | `requestRefresh()` from any task | Run a refresh |
| `St67HttpFetch` | `trigger()`, driver callbacks | Run a batch; DNS, HTTP, scan, connect, disconnect, driver error |
| Refresh caller (`AstroDataRefresh`) | `St67HttpFetch` | `kFetchFlagDone`, `kFetchFlagStage` |

The ST67 `RDY` line (`PA4`) interrupts on its rising edge;
`HAL_GPIO_EXTI_Rising_Callback()` in `User/Src/WiFi/St67SpiReady.cpp` passes it
to the driver's `spi_on_txn_data_ready()`.

### Queues

| Queue | Depth | Producer | Consumer |
| --- | ---: | --- | --- |
| `LogService` log queue | 16 `LogEvent` | any task | `LogService`; when full, the oldest entry is dropped and counted |
| `ConsoleService` command queue | 8 lines | `ConsoleService`, which assembles lines from a 256-byte ring filled by the USB receive callback | `ConsoleService` |

### Mutexes

All are `Utils::Mutex` (priority inheritance, static storage).

| Mutex | Protects |
| --- | --- |
| `I2cBus::mutex_` | One I2C1 transfer at a time: the EEPROM and the five remote boards, driven from the console and refresh tasks. The clock and current-sense tasks only use `submitLocal()` and never touch I2C. Held per transfer, not per operation. |
| `Display::submitMutex_` | The SPI/I2C transfer sequence of `submit()` and `submitLocal()`. Setters are not locked; concurrent writers are last-writer-wins. |
| `PcbDisplayBoard::frameMutex_` | The prepared local frame, between `submit()` and the `DisplayRefresh` task. |
| `ClockTask::rtcMutex_` | RTC reads and writes, from the clock task, the console and the refresh task's clock sync. |
| `Settings::Store::mutex_` | The Wi-Fi SSID and password and the API host and path, written by the console and copied by the fetch task on every connect or fetch; low brightness, written by the console and by `MainLoopTask` (switch 2); and the whole of `save()`, so a console save and a switch 2 save cannot interleave their page writes. Other settings are written only by the console task and are not locked. |

Short state shared with other tasks, such as the refresh `active_` flag, the
schedule summary and the last Wi-Fi connect result, is guarded with
`taskENTER_CRITICAL()`.

### Refresh and fetch handoff

`AstroDataRefreshTask` owns a 4096-byte `responseBuffer_` and an
`St67FetchRequest`. It calls `FetchSt67Data()`, which runs on the refresh task:
it places the request in the fetch task's single slot, sets its trigger flag and
waits on `kFetchFlagDone | kFetchFlagStage`, waking at least every 250 ms to
redraw the progress bar. The fetch task writes the body straight into the
caller's buffer, advances `request->stage` and sets `kFetchFlagStage` at each
step, and sets `kFetchFlagDone` after publishing the result. The caller gives up
after 180 s (`kClientFetchTimeoutMs`); the fetch task cannot be cancelled and
keeps the slot, so fetches report `Busy` until it finishes. Only the refresh
task touches the display during a fetch. See [WiFi.md](WiFi.md) and
[AstroRefresh.md](AstroRefresh.md).

## Peripherals and Pins

From `Core/Inc/main.h` and `HostControllerA.ioc`.

| Peripheral | Pins | Use |
| --- | --- | --- |
| SPI1, master, 2 Mbit/s | `PA1` SCK, `PA6` MISO, `PA7` MOSI | ST67W611M1, DMA1 channel 1 (RX) and 2 (TX) |
| GPIO | `PA5` `ST67_CS`, `PA0` `ST67_CHIP_EN`, `PA4` `ST67_RDY` (EXTI) | ST67 chip select, enable, ready; used by the driver's `spi_port.c` |
| SPI3, master TX only, 1 Mbit/s | `PB3` SCK, `PB5` MOSI | SCT2xxx daisy chain of the local board |
| GPIO | `PB6` `SCT_LATCH`, `PB7` `SCT_ENABLE` | SCT latch and output enable |
| GPIO | `PD3`, `PA15`, `PD1`, `PD2`, `PD0` = `DISPLAY_1_EN`..`DISPLAY_5_EN` | Multiplex selects |
| TIM2, 16 MHz / 16000 / 4 | none | 250 Hz multiplex tick, interrupt |
| I2C1 | `PA9` SCL, `PA10` SDA | 24AA04 EEPROM (`0x50`) and remote boards (`0x10`..`0x14`) |
| ADC1, 16x oversampling | `PB2` `CURRENT_SENSE` (IN10), plus the internal temperature sensor and VREFINT | Current, temperature and VDDA; DMA1 channel 3 |
| RTC | none (LSI) | Calendar and backup registers |
| USB FS device, CDC | `PA11` DM, `PA12` DP | Console |
| GPIO EXTI | `PB12` `SWITCH_1`, `PB13` `SWITCH_2` | Switches, falling edge |
| GPIO | `PC13` `LED_1`, `PB9` `LED_2` | Heartbeat, switch feedback |
| GPIO | `PB8` `LOW_POWER_EN` | Low-brightness step for every board, set by `display low` and toggled by switch 2 (HostController only; see [Display.md](Display.md#low-brightness)) |
| GPIO inputs | `PB10`, `PB11`, `PB14` = `ADDR_0`..`ADDR_2` | Board address straps, read only by the unused `detectBoardAddress()` |
| SWD | `PA13`, `PA14` | Debug |
| TIM1 | none | HAL time base |

Configured but unused:

- **USART2** (`PA2` `ST67_TX`, `PA3` `ST67_RX`, 921600 baud) is initialised by
  `MX_USART2_UART_Init()`; nothing uses `huart2`. The module is driven over SPI.
- **`ST67_BOOT`** (`PB1`) is an output driven only by the unused
  `St67ProbeTask`, so it stays at its reset level.

### RTC backup registers

Both survive a reset and are lost with power, like the calendar.

| Register | Macro | Contents |
| --- | --- | --- |
| DR0 | `RTC_TIME_SET_BKP_REGISTER` | `0x54494D45` (`TIME`) once the time has been set; checked by `MX_RTC_Init()` and `ClockTask` |
| DR1 | `RTC_ASTRO_REFRESH_BKP_REGISTER` | Time of the last successful astro refresh, seconds since 2000 local; restored into the schedule at boot |

## Memory

FreeRTOS uses `heap_4.c` with `configTOTAL_HEAP_SIZE` of 40000 bytes. The heap
holds `defaultTask`, the four middleware tasks, the CMSIS objects created
without static memory, and the ST67 and LwIP allocations; application tasks and
their stacks are static. LwIP has its own heap. See
[Firmware-RAM-Usage.md](Firmware-RAM-Usage.md) for the breakdown and
`stats on` ([Console.md](Console.md)) for live heap and stack headroom.

`configCHECK_FOR_STACK_OVERFLOW` is 2. `vApplicationStackOverflowHook()` in
`main.c` stores the task name in `g_stackOverflowTaskName`, disables interrupts
and spins. The board freezes with the display stopped and the console silent;
read `g_stackOverflowTaskName` with a debugger to find the task. There is no
watchdog, so it stays that way until reset.

## Unit Tests

The native suites in `tests/` build with the `NativeTests` preset
(`BUILD_NATIVE_TESTS=ON`) and run under CTest; see
[Development.md](Development.md). Decisions are kept out of the tasks: parsing,
mapping and timing logic lives in small hardware-free units (for example
`AstroDisplayMapper`, `AstroProgressBar`, `HttpResponseParser`,
`St67ConnectDiagnosis`), and the tasks only do the I/O around them. Code that
still needs the HAL, the RTOS or the log links the stand-ins in
`../Common/tests/stubs` and `tests/fakes`. The suites for the shared code are
a separate project in `../Common`.

The suites, what each covers, and what is still untested (console commands,
`Settings::Store` and the EEPROM driver, `BufferedDisplayBoard`, `LowBrightness`,
`resolveApiTarget()`, and the bench-only code) are listed in [Testing.md](Testing.md#coverage-by-module).

## Unused and Dead Code

Present in the tree, and compiled unless noted, but not used by the running
firmware:

- **`St67ProbeTask`** (`User/Src/WiFi/St67ProbeTask.cpp`): the raw-SPI probe from
  bring-up. `StartSt67ProbeTask()` is never called, so the linker discards it.
- **`TriggerSt67ConnectivityCycle()`**: starts the WiFi stress batch
  ([WiFi.md](WiFi.md#stress-batch)). No callers since switch 2 was given to low
  brightness; kept for bench use.
- **`TriggerSt67SmokeTest()`**: an old alias of `TriggerSt67ConnectivityCycle()`
  with no callers.
- **`Display::detectBoardAddress()`** (`../Common/Src/Display/DisplayAddress.cpp`):
  reads the `ADDR_0`..`ADDR_2` straps, meant for the DisplayController I2C
  target; no callers in this project.
- **`LWIP/App/sntp.c`, `LWIP/App/altls_mbedtls.c`**: compiled by the CubeMX CMake
  list, with no callers; discarded at link time.
- **`LWIP/App/dhcp_server_raw.c`**: linked, because the soft-AP link-up callback
  registered by `MX_LWIP_Init()` calls `dhcpd_start()`, but the firmware never
  starts a soft-AP.
- **`LWIP/App/http_client.c`**: the generated HTTP client. The firmware uses its
  own `HttpClient_Get()` in `User/Src/WiFi/HttpClient.cpp` and only borrows the
  types from `http_client.h`; the generated one is discarded at link time.
- **`Appli/App/main_app.h`, `User/Inc/logshell_ctrl.h`**: empty headers.
- **`St67Runtime::httpPayload`**: a 4 KB buffer inside the fetch task object,
  written only by the stress batches (no client request); every refresh writes to
  the refresh task's own buffer instead.
- **Unused `app_config.h` macros**: `APP_ST67_SCAN_TIMEOUT_MS`,
  `APP_ST67_SCAN_MAX_RESULTS`, `APP_ST67_HTTP_TOTAL_TIMEOUT_MS`,
  `APP_ST67_HTTP_MAX_HEADER_BYTES` (`HttpResponseParser.hpp` has its own
  2048-byte header limit, `kHeaderCapacity`), and `APP_ST67_WIFI_SSID` / `APP_ST67_WIFI_PASSWORD`, which the
  credentials template still defines although the credentials now come from the
  EEPROM (`wifi set`).
