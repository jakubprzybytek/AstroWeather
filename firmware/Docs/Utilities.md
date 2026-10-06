# Shared Utilities

The non-display code in `firmware/Common`, compiled into both the host
([HostControllerA](../HostControllerA/README.md)) and the display boards
([DisplayController](../DisplayController/README.md)). The display code and the
`SCT2xxx` LED driver are in [Display.md](Display.md), the I2C message in
[I2C.md](I2C.md).

| Code | Location |
| --- | --- |
| `Task`, `TaskBase` | `Common/Inc/Utils/Task.hpp`, `TaskBase.hpp`, `Src/Utils/TaskBase.cpp` |
| `Mutex`, `MutexGuard` | `Common/Inc/Utils/Mutex.hpp`, `Src/Utils/Mutex.cpp` |
| `SwitchInput` | `Common/Inc/Utils/SwitchInput.hpp`, `Src/Utils/SwitchInput.cpp` |
| `Crc32` | `Common/Inc/Utils/Crc32.hpp` (header only) |
| `BlinkingLed` | `Common/Inc/Debug/BlinkingLed.hpp`, `Src/Debug/BlinkingLed.cpp` |
| `PulseLed`, `activityLed()`, `ActivityLed_Pulse()` | `Common/Inc/Debug/PulseLed.hpp`, `ActivityLedBridge.h`, `Src/Debug/PulseLed.cpp` |

## Tasks

`Task<N>` is a CMSIS-RTOS2 thread that holds its stack and control block as
members, so a task object defined at file scope needs no heap. **`N` is in
bytes**, passed straight to `osThreadNew()` as `stack_size`. A subclass
implements `run()`, is constructed with a name and a priority (default
`osPriorityNormal`), and is started with `start()`.

`TaskBase` keeps a registry of up to 16 started tasks (`kMaxRegisteredTasks`).
`TaskBase::visitAll()` walks it; the host's `stats on` uses it to report each
task's stack size and high-water mark. Tasks created by middleware, from the
FreeRTOS heap, are not in it.

## Mutexes

`Utils::Mutex` is a non-recursive CMSIS-RTOS2 mutex with priority inheritance
whose control block is a member, never heap-allocated, so it can be a global or
static object; it creates the FreeRTOS mutex at construction, before the
scheduler starts. `MutexGuard` locks it for a scope. Neither may be used from an
interrupt.

## Switches

`SwitchInput` turns presses of `SWITCH_1` and `SWITCH_2` into thread flags. It
defines `HAL_GPIO_EXTI_Falling_Callback()`, so a falling edge on either pin, in
the EXTI interrupt, sets the flag `attach()` chose for it on the recipient task;
`detach()` stops that. Each board's CubeMX configuration sets both pins to EXTI
on the falling edge with the internal pull-up; the host board adds external
pull-ups and an RC filter, while on a display board the buttons and their
resistors are optional. There is no software debounce. The rising-edge callback
is left to the project (the host uses it for the ST67 `RDY` line).

## CRC-32

`Crc32` is the standard reflected CRC-32 (IEEE 802.3, zlib, PNG): polynomial
`0x04C11DB7` reflected to `0xEDB88320`, initial value and final XOR
`0xFFFFFFFF`, computed bit by bit with no table. `compute()` does one buffer;
`kInitial`, `update()` and `finish()` handle data that arrives in pieces. The
check value of `"123456789"` is `0xCBF43926`. The host uses it to check the
forecast payload.

## LEDs

- **`LED_1`, heartbeat.** `BlinkingLed` is a `Task<768>` at low priority that
  lights an active-high LED for `onMs`, then leaves it off for `offMs`. Both
  boards run it on `LED_1` from power-up at `kHeartbeatOnMs`/`kHeartbeatOffMs`,
  20 ms every 2 s, so a running board looks the same whichever it is.
- **`LED_2`, activity.** `PulseLed::pulse(ms)` lights the LED at once and a
  static FreeRTOS one-shot timer clears it, so a pulse never blocks and works
  from an interrupt; overlapping pulses merge into one. `init()` creates the
  timer; pulses before that are ignored. `activityLed()` is the board's
  `LED_2`, and `ActivityLed_Pulse(ms)` in `ActivityLedBridge.h` reaches it from
  C code such as the USB device files. The host pulses it for switch presses and
  USB CDC traffic, a display board for every I2C transaction addressed to it.

## Rules for Shared Code

Each project compiles `${COMMON_SOURCES}` from `Common/CommonSources.cmake`
itself, against its own HAL, device define (`STM32G0B1xx` or `STM32G070xx`),
`stm32g0xx_hal_conf.h`, `FreeRTOSConfig.h` and `main.h`, so a change here must
be built in both projects. Code in `Common`:

- may include `main.h`, `cmsis_os2.h` and `FreeRTOS.h`, and may use the pin
  labels both CubeMX projects define: `LED_1`, `LED_2`, `SWITCH_1`, `SWITCH_2`,
  `ADDR_0`..`ADDR_2`, `DISPLAY_1_EN`..`DISPLAY_5_EN`, `SCT_LATCH`, `SCT_ENABLE`;
- is given peripheral handles (`hspi3` on the host, `hspi1` on the display
  board, the refresh timer) by the project, and never names them;
- must not depend on anything only one project has: the host's `LogService`,
  USB, the settings store, the EEPROM, Wi-Fi.

Its tests are described in [Testing.md](Testing.md#common-suites).
