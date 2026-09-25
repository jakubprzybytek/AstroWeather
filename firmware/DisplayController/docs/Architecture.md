# DisplayController Architecture

How the display board firmware is put together. The display itself (content
types, PCB mapping, multiplexing) and the I2C message format are shared with
the host and described in
[HostControllerA/docs/Display.md](../../HostControllerA/docs/Display.md); pins
and peripherals are in the [README](../README.md#hardware).

None of this has run on a display board yet: it compiles, and the screens and
the stale-data timeout are unit tested.

## Boot

`main()` runs the CubeMX sequence: `HAL_Init()`, `SystemClock_Config()` (HSI,
16 MHz), `MX_GPIO_Init()`, `MX_SPI1_Init()`, `MX_I2C1_Init()`,
`MX_TIM6_Init()`, `osKernelInitialize()`, `defaultTask`, then
`DisplayController_Init()` from the `RTOS_THREADS` user section, then
`osKernelStart()`.

`DisplayController_Init()` (`User/Src/DisplayController.cpp`), before the
scheduler starts:

1. Starts the `Led1` heartbeat.
2. Loads the all-segments self-test into the board and starts the refresh
   (`PcbDisplayBoard::start()`: enables the SCT outputs, starts the
   `DisplayRefresh` task and TIM6). The self-test is prepared first, so the
   first frames latched are the self-test, not whatever the drivers held at
   reset.
3. Starts the `DisplayApp` task and routes the switches to it
   (`Utils::SwitchInput`).
4. Reads the address straps (`Display::detectBoardAddress()`), stores the
   address in `g_displayStats.address`, and starts the I2C target on it
   (`I2cTarget::begin()`).

CubeMX enables I2C1 with a placeholder own address, `0x10`, in
`MX_I2C1_Init()`. Nothing services it until step 4 re-initialises the
peripheral with the real address, a few milliseconds later. A host write to
`0x10` in that window would be acknowledged and then held; the host gives up
after its 50 ms timeout.

Then `DisplayApp` shows, in order: the self-test for 1 s, the board's address
for 2 s, and then the host's data, or "no data" if none has arrived yet.

## Tasks

| Task | Owner | Priority | Stack (bytes) | Does |
| --- | --- | --- | ---: | --- |
| `DisplayRefresh` | `Display::PcbDisplayBoard` (Common) | Realtime (48) | 1024 | Multiplexes the board, one slot per TIM6 tick (250 Hz) |
| `DisplayApp` | `User/Src/DisplayApp.cpp` | Normal (24) | 1024 | Chooses what is shown: boot screens, data, "no data", test screens |
| `defaultTask` | `Core/Src/main.c` | Normal (24) | 512 | Idles |
| `Led1` | `Debug::BlinkingLed` (Common) | Low (8) | 768 | Heartbeat on `LED_1`, 20 ms every 2 s |

`DisplayRefresh`, `DisplayApp` and `Led1` are `Task<N>` objects with static
stacks, and `Utils::Mutex` uses static storage, so the 3072-byte FreeRTOS heap
holds only `defaultTask`. `configCHECK_FOR_STACK_OVERFLOW` is 2; see
[Diagnostics](#diagnostics).

`DisplayApp` waits on three thread flags, with a 1 s timeout for its periodic
checks:

| Flag | Set by | Meaning |
| --- | --- | --- |
| `kFlagFrame` | `I2cTarget`, from the I2C interrupt | A complete 36-byte message is waiting |
| `kFlagSwitch1` | `SwitchInput`, from the EXTI interrupt | Switch 1 pressed |
| `kFlagSwitch2` | `SwitchInput` | Switch 2 pressed |

Every second it also checks whether the data has gone stale, closes a test
screen that has been up too long, and restarts I2C listening if it has
stopped.

## Screens

`DisplayApp` shows one of these at a time; the contents come from
`User/Src/Screens.cpp` and `Display::noDataState()`:

| Screen | Contents | Shown |
| --- | --- | --- |
| All segments | Every digit segment with its dot, L1-L3, every matrix dot | 1 s at boot; switch 1 |
| Address | `Ad12` (for 0x12) on every numeric display, matrix blank; `Ad--` if the straps gave no address | 2 s at boot; 3 s on switch 2 |
| Identify | Numeric display *n* (0-3) shows *n* + 1 on all four digits (`1111` to `4444`); matrix row *r* lights its first *r* + 1 columns, so the top row has one dot | Switch 1, after all segments |
| Data | The last message from the host | After a frame arrives, until it goes stale |
| No data | Segment G on the last digit of every numeric display (`   -`), everything else off, matrix blank | Before the first frame, and after 7 h without one |

Switch 1 steps through all segments, identify, and back to the data; each test
screen closes by itself after 60 s. A frame that arrives while a test or
address screen is up is kept and shown when it closes. `LED_2` flashes for
20 ms on every accepted frame.

The stale-data timeout is 7 hours (`DisplayApp::kNoDataTimeoutMs`), just over
the host's 6-hour refresh interval: the host sends to the remote boards only on
an astro refresh or a `display` command, so a shorter timeout would show "no
data" for most of the day. One missed refresh is therefore enough to show it.
`NoDataTimer` (`User/Inc/NoDataTimer.hpp`) does the arithmetic on the wrapping
kernel tick.

## I2C Target

`I2cTarget` (`User/Src/I2cTarget.cpp`) owns `hi2c1` in target mode, using the
HAL's interrupt-driven sequential listen API:

- `HAL_I2C_AddrCallback()`: the host addressed this board. For a write, a
  36-byte receive is started (`I2C_FIRST_AND_LAST_FRAME`). For a read, which
  the protocol does not use, one byte (0) is sent so the bus is not held.
- `HAL_I2C_SlaveRxCpltCallback()`: all 36 bytes arrived. They are copied to a
  second buffer and `kFlagFrame` is set; only the newest message is kept.
- `HAL_I2C_ListenCpltCallback()` and `HAL_I2C_ErrorCallback()`: the transfer
  ended. A write that ended before 36 bytes is counted as a probe (no data
  bytes, as the host's `status` sends) or a short write, and listening is
  restarted.

The task decodes the message with `Display::deserializeI2c()`: command `0x01`
and exactly 36 bytes, or it is rejected and the previous frame stays. Nothing
is decoded in the interrupt.

The HAL NACKs the byte after the 36th; a host that sends more gets an error on
its side. Listening is restarted after every error, and the task checks every
second that the peripheral is still listening (`ensureListening()`), which
also recovers from a bus error the HAL left in the ready state. Listening is
never started without a strap address, so a board cannot answer on another
board's address.

## Diagnostics

Until the board has a console, its counters are read over SWD. They are in
`g_displayStats` (`User/Inc/Stats.hpp`), a C-linkage struct of nine 32-bit
fields:

| Field | Counts |
| --- | --- |
| `address` | The 7-bit address from the straps, 0 if none |
| `framesAccepted` | Messages shown |
| `framesRejected` | 36-byte messages with an unknown command |
| `shortWrites` | Writes that ended before 36 bytes |
| `probes` | Address-only writes, such as the host's `status` probe |
| `i2cErrors` | Bus errors other than a NACK |
| `listenRearms` | Times the periodic check had to restart listening |
| `staleTimeouts` | Times the data went stale and "no data" was shown |
| `lastFrameTick` | Kernel tick (ms since boot) of the last accepted frame |

```bash
arm-none-eabi-nm build/Debug/DisplayController.elf | grep g_displayStats
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -r32 <address> 0x24
```

`mode=HOTPLUG` attaches without resetting the board.

A stack overflow halts the board in `vApplicationStackOverflowHook()`
(`Core/Src/main.c`), with interrupts disabled and the task's name in
`g_stackOverflowTaskName`, as on the host. The display then freezes on its
current slot.

## Code Layout

| File | Content |
| --- | --- |
| `User/Src/DisplayController.cpp` | The static objects and `DisplayController_Init()` |
| `User/Src/DisplayApp.cpp` | The `DisplayApp` task |
| `User/Src/I2cTarget.cpp` | The I2C target and the HAL I2C callbacks |
| `User/Src/Screens.cpp` | Self-test, identify and address screens |
| `User/Inc/NoDataTimer.hpp` | Stale-data timeout on the wrapping tick |
| `User/Inc/Stats.hpp` | `g_displayStats` |
| `tests/ScreensTests.cpp`, `tests/NoDataTimerTests.cpp` | Native tests |
| `../Common` | Display types and encoding, `PcbDisplayBoard`, `SCT2xxx`, the I2C message, the address straps, tasks and mutexes |

## Open Items

- Run it on a display board: the refresh on SPI1/TIM6, the I2C target against
  the host, and the boot and test screens.
- Console over USART2 (PA2/PA3); see the README.
