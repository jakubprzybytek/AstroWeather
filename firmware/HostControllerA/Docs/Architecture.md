# Firmware Architecture

## Overview

This page describes how the firmware is put together: how it boots, which
tasks run and how they talk to each other, which peripherals and pins it uses,
which interrupts it enables and at what priority, what the unit tests cover,
and which code is present but unused. The feature
documents listed in the [README](../README.md#documentation) describe each
subsystem in depth.

The firmware is C++17 on top of the STM32CubeMX-generated C code for an
STM32G0B1CETx (Cortex-M0+, 16 MHz from HSI16, 512 KB flash, 144 KB RAM),
FreeRTOS through CMSIS-RTOS2 and the ST67W6X network driver in its T01
architecture, where TCP/IP and TLS run in the Wi-Fi module. Application
code lives under `User/` and, for the parts shared with the DisplayController
firmware, under `../Common/` (see [Shared Code](#shared-code)); CubeMX owns
`Core/`, `Drivers/`, `Middlewares/`,
`USB_Device/` and `ST67W6X_Network_Driver/`, and application changes
there stay inside `USER CODE` sections (see
[Development.md](../../Docs/Development.md#cubemx-compliance)).

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

`AstroWeather_Init()` first creates the timer of the `activityLed()`
`PulseLed` on `LED_2`, then, in order (`LED_1`, the heartbeat, needs no setup:
the refresh interrupt drives it once `localBoard` starts):

1. `LogService` `init()` (creates the log queue) and `start()`.
2. `Display::detectBoardAddress()` reads the host's `ADDR_0`..`ADDR_2` straps,
   as a display board does, and `display.setLocalAddress()` takes the result
   before anything submits: it decides which forecast block the local board
   shows and which remote address is skipped (see
   [AstroRefresh.md](AstroRefresh.md#display-mapping)). The address is logged,
   with a warning if it is outside `0x10`–`0x15`, but USB CDC has not
   enumerated yet, so the line is normally lost (the warning is also kept in
   `errors`); `status` shows the address as `host` on the `remote` line.
3. `settingsStore.load()` reads the EEPROM; the outcome, not the values, is
   logged. This works before the scheduler because EEPROM reads take no
   `osDelay` and the bus mutex is uncontended. A saved HSI trim other than 64
   is applied at once (`HsiTrim::set()`, logged as `HSI trim <n>`), so the
   refresh timeline runs at the trimmed rate from its first frame; see
   [TimelineSync.md](../../Docs/TimelineSync.md#trimming-the-hosts-hsi).
4. `CurrentSenseTask`: logging and display flags from settings, the display,
   then `start()`.
5. `ConsoleService`: `init(&display)`, EEPROM and settings pointers, `start()`.
6. `LowBrightness::set()` applies the saved low brightness to `PB8`, before the
   displays light up.
7. The local board gets the "no data" state (`Display::noDataState()`, see
   [Display.md](../../Docs/Display.md#time-blank-and-no-data)) but does not show it yet: the first
   slot-test frame (`Display::slotTestState(0)`) is shown instead, then
   `localBoard.start()` enables the SCT outputs and starts TIM2, whose
   interrupt refreshes the board, keeps the refresh timeline and drives the
   `LED_1` heartbeat from then on. `Display` holds back every
   local submit until `MainLoopTask` has run the boot screens.
8. `ClockTask`: display flag and trim from settings (an error is logged if the
   trim is rejected), the display, `start()`.
9. `DisplaySyncTask`: `init(&i2c1Bus, &display)`, `start()`; after
   `localBoard.start()`, since the sync carries its refresh timeline
   ([TimelineSync.md](../../Docs/TimelineSync.md#syncs-from-the-host)).
10. `SetSt67CredentialSource(&settingsStore)`, then `StartSt67HttpFetchTask()`.
   The credential source must be set first, since the fetch task reads the
   credentials on every connect.
11. `AstroDataRefreshTask`: `init(&display)` restores the last successful
    refresh time from backup register DR1 when the RTC is set, then `start()`.
12. `MainLoopTask`: `init(activityLed(), &settingsStore, &display)` (switch 2
    saves low brightness), `start()`. Its first act once the scheduler runs is
    `display.runBootScreens()`: each `DISPLAYx_EN` slot lit on its own for
    200 ms, then `AdNN` (the host's strap address) on numeric display 1 for
    2 s, then the local board's own state, which clients have kept writing
    meanwhile. See [DisplayController Architecture](../../DisplayController/Docs/Architecture.md#screens).
13. `SwitchInput::attach()` routes the `SWITCH_1`/`SWITCH_2` EXTI interrupts to
    `MainLoopTask` as thread flags.

`defaultTask` initialises the USB device (`MX_USB_Device_Init()`) once the
scheduler runs and then ends itself (`osThreadExit()` in the `USER CODE 5`
section): USB runs from its interrupt, and the idle task returns the stack to
the heap. CubeMX does not allow removing the task, so this keeps it from ever
waking again.

## Shared Code

The remote display boards run the separate
[DisplayController](../../DisplayController/README.md) project, an STM32G070.
Code both images need lives in [`../Common`](../../Common/README.md) and is
compiled into each project against that project's HAL, `main.h` and FreeRTOS
configuration, so the two CubeMX projects keep the same labels for the pins it
uses. It is documented once, for both, in [firmware/Docs](../../Docs/README.md):
the display model, encoding, refresh and timeline sync in
[Display.md](../../Docs/Display.md), the I2C link in
[I2C.md](../../Docs/I2C.md), and the tasks, mutexes, switches and LEDs in
[Utilities.md](../../Docs/Utilities.md).

Everything else under `User/` is host-only: `Astro/` (the refresh task, parser,
mapper, progress bar and schedule), `Clock/`, `WiFi/`, `Console/`, `Settings/`,
`Sensors/`, `LogService`, the EEPROM driver and `I2cBus`, the aggregate
`Display` with its `BufferedDisplayBoard`s, `DisplaySyncTask` and its
`SyncSchedule`, `LowBrightness` and `MainLoopTask`.
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
| `localBoard` | `Display::PcbDisplayBoard` | `localSct`, `htim2`, `DISPLAY_1_EN`..`DISPLAY_5_EN`; refreshed from TIM2's interrupt, which also keeps the refresh timeline the display boards follow and drives the `LED_1` heartbeat |
| `remoteBoard10`..`remoteBoard15` | `Display::BufferedDisplayBoard` | `i2c1Bus` at 7-bit addresses `0x10`..`0x15`, one per forecast block; the one at the host's own address is unused |
| `display` | `Display::Display` | `localBoard` plus the six remote boards; `AstroWeather_Init()` sets the host's address from its straps (`setLocalAddress()`) |
| `settingsEeprom` | `Device::Eeprom24AA04` | `i2c1Bus`, address `0x50` |
| `settingsStore` | `Settings::Store` | `settingsEeprom` and the in-RAM `Values` |
| `activityLed()` | `PulseLed` (`../Common/Src/Debug/PulseLed.cpp`) | `LED_2`: a 250 ms pulse for switch 1, 50 ms for switch 2 (`MainLoopTask`), and 20 ms for every USB CDC transfer in either direction (`CDC_Receive_FS` in the USB interrupt, `CDC_Transmit_FS` after a successful send). The pin is set at once and a static FreeRTOS one-shot timer clears it, so a pulse never blocks and works from an interrupt; overlapping pulses merge. |

The application tasks are singletons reached through `instance()`. The fetch task is private to `User/Src/WiFi/St67HttpFetchTask.cpp`
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
`AboveNormal` = 32, `Realtime` = 48.

The local display is not refreshed by a task: `Display::PcbDisplayBoard`
runs the multiplexing from TIM2's update interrupt, with SPI3 transfers by
DMA, so its timing depends on interrupt latency only; see
[Display.md](../../Docs/Display.md#refresh-operation).

| Task name | Owner | Priority | Stack (bytes) | Allocation | Does |
| --- | --- | --- | ---: | --- | --- |
| `Modem_Process` | ST67 driver `w61_at_common.c` | 47 | 2048 | heap | AT response and event handling |
| `spi_xfer_engine` | ST67 driver `spi_iface.c` | 46 | 1536 | heap | SPI1 transfers to the module |
| `DisplaySync` | `Display/DisplaySyncTask.cpp` | AboveNormal (32) | 2048 | static | Broadcasts the refresh timeline to the display boards and polls their sync status; a few milliseconds a minute, above the normal tasks so a due sync is not held up ([TimelineSync.md](../../Docs/TimelineSync.md#syncs-from-the-host)) |
| `defaultTask` | `Core/Src/main.c` | Normal (24) | 512 | heap | Starts USB, then exits (stack freed) |
| `LogService` | `Debug/LogService.cpp` | Normal (24) | 1536 | static | Drains the log queue to USB CDC; `stats` output |
| `ConsoleService` | `Console/ConsoleService.cpp` | Normal (24) | 2304 | static | Assembles and runs console commands |
| `AstroDataRefresh` | `Astro/AstroDataRefreshTask.cpp` | Normal (24) | 3328 | static | Refresh pipeline, 6-hourly schedule, progress bar |
| `MainLoopTask` | `MainLoopTask.cpp` | Normal (24) | 2048 | static | Boot screens on the local board (about 3 s, 944 B of stack at peak), then switch presses: switch 1 requests a refresh, switch 2 toggles low brightness and saves it |
| `CurrentSense` | `Sensors/CurrentSenseTask.cpp` | BelowNormal (16) | 2048 | static | ADC every 100 ms, idle while `adc display` and `adc log` are both off |
| `Clock` | `Clock/ClockTask.cpp` | BelowNormal (16) | 1024 | static | RTC, `HH:MM` on display 3 |
| `St67HttpFetch` | `WiFi/St67HttpFetchTask.cpp` | BelowNormal (16) | 4096 | static | Owns the ST67 session and the HTTPS fetch |
| `Tmr Svc` | FreeRTOS | 2 | 1024 | static | FreeRTOS timer service |
| `IDLE` | FreeRTOS | 0 | 512 | static | Idle |

The two middleware tasks are created on the first fetch by `W6X_Init()`. After
an ordinary refresh the module stays up, so they persist. Their priorities, 46
and 47, and the SPI engine's 1536-byte stack are set in the `USER CODE` block of
`ST67W6X_Network_Driver/Target/w61_driver_config.h` (see
[Development.md](../../Docs/Development.md#cubemx-compliance)). No task
priority affects the display, which is refreshed from TIM2's interrupt.

## Interrupt Priorities

The Cortex-M0+ has four interrupt priority levels, 0 (highest) to 3. It has no
`BASEPRI`, so a FreeRTOS critical section masks every interrupt (`PRIMASK`);
the levels only decide which interrupt runs first and which may preempt
another. The application uses two of them. Set in `HostControllerA.ioc` and
generated into `MX_DMA_Init()`/`MX_GPIO_Init()` (`Core/Src/main.c`), the MSP
functions (`Core/Src/stm32g0xx_hal_msp.c`) and `HAL_PCD_MspInit()`
(`USB_Device/Target/usbd_conf.c`):

| Interrupt | Priority | Source |
| --- | ---: | --- |
| `USB_UCPD1_2` | 1 | USB FS device: the CDC console |
| `TIM2` | 3 | Display refresh, once per pass ([Display.md](../../Docs/Display.md#refresh-operation)) |
| `DMA1_Channel1` | 3 | SPI1 RX, ST67 module |
| `DMA1_Channel2_3` | 3 | SPI1 TX (channel 2), ST67 module; ADC1 (channel 3), current sense |
| `DMA1_Ch4_7_DMA2_Ch1_5_DMAMUX1_OVR` | 3 | SPI3 TX (channel 4), the local display's SCT chain |
| `EXTI4_15` | 3 | `ST67_RDY` (`PA4`), `SWITCH_1` (`PB12`), `SWITCH_2` (`PB13`) |
| `ADC1_COMP` | 3 | ADC1 |
| `TIM1_BRK_UP_TRG_COM` | 3 | HAL time base (`TICK_INT_PRIORITY`) |
| `SysTick`, `PendSV` | 3 | FreeRTOS tick and context switch |

I2C1 has no interrupt: the host is a polled master with timeouts (see
[I2C.md](../../Docs/I2C.md)).

USB is the one interrupt above the rest because the USB peripheral does not
answer a new SETUP packet while the previous endpoint-0 interrupt is still
pending, and the host gives up after three tries; queued behind a refresh
interrupt (up to about 170 us) it missed requests, so the COM port failed to
open or enumerate. See [Console.md](Console.md#usb-device). The refresh
interrupt can in turn be delayed by a USB interrupt (100-290 us in the Debug
build); its shortest pass is 480 us, and a late one is counted as
`late interrupts` in `status`.

## Inter-task Communication

Tasks wake on CMSIS thread flags almost everywhere; there are two message
queues and five mutexes.

### Thread flags

| Receiver | Set by | Meaning |
| --- | --- | --- |
| `MainLoopTask` | `HAL_GPIO_EXTI_Falling_Callback()` via `SwitchInput` | `kEventSwitch1`, `kEventSwitch2` |
| `CurrentSense` | ADC DMA complete / error callbacks | Conversion done or failed |
| `CurrentSense` | `setDisplayEnabled()`, `setLoggingEnabled()` | Re-check whether to sample |
| `ConsoleService` | USB CDC RX and line-state callbacks | Command queued, host connected |
| `LogService` | `log()` from any task; `setStatsEnabled()` | Log queued, stats changed |
| `Clock` | display and time setters | Redraw |
| `AstroDataRefresh` | `requestRefresh()` from any task | Run a refresh |
| `DisplaySync` | `requestBurst()` (`time sync now`) | Start a burst of syncs |
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
| `I2cBus::mutex_` | One I2C1 transfer at a time: the EEPROM and the five remote boards, driven from the console, refresh and `DisplaySync` tasks. The clock and current-sense tasks only use `submitLocal()` and never touch I2C. Held per transfer, not per operation; the sync broadcast also suspends the scheduler for its transfer, about 1 ms ([I2C.md](../../Docs/I2C.md#devicei2cbus)). |
| `Display::submitMutex_` | The SPI/I2C transfer sequence of `submit()` and `submitLocal()`. Setters are not locked; concurrent writers are last-writer-wins. |
| `PcbDisplayBoard::submitMutex_` | One `submit()` at a time encoding into the back frame set; the refresh interrupt takes no lock and swaps the sets at a frame boundary. |
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
| TIM2, 16 MHz / 16 = 1 MHz count | none | Display refresh: the interrupt sets each pass's length (four passes per 4 ms slot, 50 Hz frames) |
| I2C1 | `PA9` SCL, `PA10` SDA | 24AA04 EEPROM (`0x50`), remote boards (`0x10`..`0x15`) and the timeline sync broadcast (general call `0x00`) |
| ADC1, 16x oversampling | `PB2` `CURRENT_SENSE` (IN10), plus the internal temperature sensor and VREFINT | Current, temperature and VDDA; DMA1 channel 3 |
| RTC | none (LSI) | Calendar and backup registers |
| USB FS device, CDC | `PA11` DM, `PA12` DP | Console |
| GPIO EXTI | `PB12` `SWITCH_1`, `PB13` `SWITCH_2` | Switches, falling edge |
| GPIO | `PC13` `LED_1`, `PB9` `LED_2` | Heartbeat from the refresh interrupt; switch presses and USB CDC traffic |
| GPIO | `PB8` `LOW_POWER_EN` | Low-brightness step for every board, set by `display low` and toggled by switch 2 (HostController only; see [Display.md](Display.md#low-brightness)) |
| GPIO inputs | `PB10`, `PB11`, `PB14` = `ADDR_0`..`ADDR_2` | Board address straps, read once at boot by `detectBoardAddress()`, then left analog |
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

FreeRTOS uses `heap_4.c` with `configTOTAL_HEAP_SIZE` of 32000 bytes, against
a measured peak use of about 15.9 KB. The heap
holds `defaultTask` until it exits after starting USB, the two middleware tasks, the CMSIS objects created
without static memory, and the ST67 driver's allocations, including the HTTP
client's per-request buffers; application tasks and their stacks are static. See
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
[Testing.md](../../Docs/Testing.md). Decisions are kept out of the tasks: parsing,
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
- **The driver's HTTP, MQTT and BLE services** (`Middlewares/ST/ST67W6X_Network_Driver/Core/w6x_http.c`,
  `w6x_mqtt.c`, `w6x_ble.c`): compiled by the CubeMX CMake list, with no
  callers; discarded at link time. The firmware uses its own
  `HttpClient::get()` in `User/Src/WiFi/HttpClient.cpp`; see
  [WiFi.md](WiFi.md#why-a-user-owned-http-client).
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
