# DisplayController Architecture

How the display board firmware is put together. The display itself (content
types, PCB mapping, multiplexing) and the I2C message format are shared with
the host and described in
[HostControllerA/docs/Display.md](../../HostControllerA/Docs/Display.md); pins
and peripherals are in the [README](../README.md#hardware).

None of this has run on a display board yet: it compiles, and the screens and
the stale-data timeout are unit tested.

## Boot

`main()` runs the CubeMX sequence: `HAL_Init()`, `SystemClock_Config()` (HSI,
16 MHz), `MX_GPIO_Init()`, `MX_DMA_Init()`, `MX_SPI1_Init()`, `MX_I2C1_Init()`,
`MX_TIM6_Init()`, `osKernelInitialize()`, `defaultTask`, then
`DisplayController_Init()` from the `RTOS_THREADS` user section, then
`osKernelStart()`.

`DisplayController_Init()` (`User/Src/DisplayController.cpp`), before the
scheduler starts:

1. Starts the `Led1` heartbeat and creates the `activityLed()` timer for
   `LED_2`.
2. Shows the first slot-test frame (`Display::slotTestState(0)`) and starts the refresh
   (`PcbDisplayBoard::start()`: enables the SCT outputs and starts TIM6, whose
   interrupt then multiplexes the board with SPI1 DMA transfers, no task
   involved; see the host's
   [Display.md](../../HostControllerA/Docs/Display.md#refresh-operation)).
   The test frame is prepared first, so the first frames latched are the
   test, not whatever the drivers held at reset.
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

Then `DisplayApp` runs the boot screens shared with the host
(`Display::showBootScreens()` in `../Common/Src/Display/BootScreens.cpp`): the
slot test, each of the five `DISPLAYx_EN` slots lit on its own for 200 ms (1 s
in all), then the board's address for 2 s. After that it shows the host's
data, or "no data" if none has arrived yet.

## Tasks

| Task | Owner | Priority | Stack (bytes) | Does |
| --- | --- | --- | ---: | --- |
| `DisplayApp` | `User/Src/DisplayApp.cpp` | Normal (24) | 2048 (peaks at ~1056 on the board) | Chooses what is shown: boot screens, data with its attributes, "no data", test screens |
| `defaultTask` | `Core/Src/main.c` | Normal (24) | 512 | Exits at once (`osThreadExit()`); CubeMX does not allow removing it. It used to wake every tick |
| `Led1` | `Debug::BlinkingLed` (Common) | Low (8) | 768 | Heartbeat on `LED_1`, 20 ms every 2 s |

`DisplayApp` and `Led1` are `Task<N>` objects with static stacks, and
`Utils::Mutex` uses static storage, so the 3072-byte FreeRTOS heap holds only
`defaultTask`, and only until it exits, just after the scheduler starts. The display refresh runs from TIM6's interrupt, not a task. `configCHECK_FOR_STACK_OVERFLOW` is 2; see
[Diagnostics](#diagnostics).

`DisplayApp` waits on three thread flags, with a 1 s timeout for its periodic
checks:

| Flag | Set by | Meaning |
| --- | --- | --- |
| `kFlagFrame` | `I2cTarget`, from the I2C interrupt | One or more complete 36-byte messages are waiting |
| `kFlagSwitch1` | `SwitchInput`, from the EXTI interrupt | Switch 1 pressed |
| `kFlagSwitch2` | `SwitchInput` | Switch 2 pressed |

Every second it also checks whether the data has gone stale, closes a test
screen that has been up too long, and restarts I2C listening if it has
stopped.

## Screens

`DisplayApp` shows one of these at a time; the contents come from
`User/Src/Screens.cpp`, `../Common/Src/Display/BootScreens.cpp` and
`Display::noDataState()`:

| Screen | Contents | Shown |
| --- | --- | --- |
| Slot test | One `DISPLAYx_EN` slot at a time, everything it drives: digit *n* of every numeric display with its dot (slot 5: L1-L3) and the matrix row it drives (row 4 - *n*). A dead slot switch shows as a step with nothing lit | At boot, 200 ms per slot, 1 s in all |
| All segments | Every digit segment with its dot, L1-L3, every matrix dot | Switch 1 |
| Address | `Ad12` (for 0x12) on numeric display 1, everything else blank; `Ad--` if the straps gave no address | 2 s at boot, after the slot test; 3 s on switch 2 |
| Identify | Numeric display *n* (0-3) shows *n* + 1 on all four digits (`1111` to `4444`); matrix row *r* lights its first *r* + 1 columns, so the top row has one dot | Switch 1, after all segments |
| Data | The last content from the host, with the blink and level attributes it sent | After a frame arrives, until it goes stale |
| No data | Segment G on the last digit of every numeric display (`   -`), everything else off, matrix blank | Before the first frame, and after 7 h without one |

The test screens and "no data" are plain: full brightness, nothing blinking.
Switch 1 steps through all segments, identify, and back to the data; each test
screen closes by itself after 60 s. A frame that arrives while a test or
address screen is up is kept and shown when it closes. `LED_2` flashes for
20 ms on every I2C transaction addressed to the board (frames, attributes,
the host's probes and reads), from the address-match interrupt
(`I2cTarget::onAddress()`) through the shared `PulseLed`; transactions
closer together than that merge into one flash.

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
- `HAL_I2C_SlaveRxCpltCallback()`: all 36 bytes arrived. They are queued
  (four deep, the oldest dropped and counted when full: the host sends a
  board's three attribute planes and its content a few milliseconds apart)
  and `kFlagFrame` is set.
- `HAL_I2C_ListenCpltCallback()` and `HAL_I2C_ErrorCallback()`: the transfer
  ended. A write that ended before 36 bytes is counted as a probe (no data
  bytes, as the host's `status` sends) or a short write, and listening is
  restarted.

The task takes every queued message and feeds it to `FrameAssembler`
(`User/Src/FrameAssembler.cpp`), which decodes it with
`Display::deserializePlaneI2c()`: a known command and exactly 36 bytes, or it
is rejected and nothing changes. The attribute commands `0x02`-`0x04` (blink,
level bit 0, level bit 1) are staged; the content command `0x01` takes effect
together with whatever is staged by then, so content and attributes always
change as one. Staged attributes persist until the host replaces them, so a
host that sends only content keeps the last attributes, and one that never
sends any gets full brightness and no blinking. Nothing is decoded in the
interrupt. The message format is in the host's
[Display.md](../../HostControllerA/Docs/Display.md#i2c-transport).

The HAL NACKs the byte after the 36th; a host that sends more gets an error on
its side. Listening is restarted after every error, and the task checks every
second that the peripheral is still listening (`ensureListening()`), which
also recovers from a bus error the HAL left in the ready state. Listening is
never started without a strap address, so a board cannot answer on another
board's address.

The I2C interrupt has priority 1, above the refresh timer, DMA and EXTI (3).
At 3, shared with the refresh interrupt (about 170 us), it could reach a
message's STOP only after the host's next address had matched (the host
starts the next message about 100 us after a STOP). The HAL sets `CR2.NACK`
when it handles a STOP, and software cannot clear that bit, only an address
match, a STOP or a sent NACK can; the late NACK then stayed set and the board
refused the next message's second byte. The host saw error `0x4` about one
refresh in seven and its retry covered it (see its
[Display.md](../../HostControllerA/Docs/Display.md#i2c-transport)). Traced
on the board on 2026-10-05: every failure was a short write of exactly one
byte, after a STOP handled late (`stopWithAddrPending`), with no bus error. A digital noise filter (15 clocks)
made no difference.

## Diagnostics

Until the board has a console, its counters are read over SWD. They are in
`g_displayStats` (`User/Inc/Stats.hpp`), a C-linkage struct of seventeen 32-bit
fields:

| Field | Counts |
| --- | --- |
| `address` | The 7-bit address from the straps, 0 if none |
| `framesAccepted` | Content messages (`0x01`) shown |
| `framesRejected` | 36-byte messages with an unknown command |
| `shortWrites` | Writes that ended before 36 bytes |
| `probes` | Address-only writes, such as the host's `status` probe |
| `i2cErrors` | Bus errors other than a NACK |
| `listenRearms` | Times the periodic check had to restart listening |
| `staleTimeouts` | Times the data went stale and "no data" was shown |
| `lastFrameTick` | Kernel tick (ms since boot) of the last accepted frame |
| `attributesAccepted` | Attribute messages (`0x02`-`0x04`) staged |
| `queueOverruns` | Messages dropped because the four-deep queue was full |
| `refreshFrames` | Display frames shown, copied from the refresh once a second; grows by 50 a second |
| `lateShifts` | Refresh interrupts that found the previous pass's shift still running |
| `lateInterrupts` | Refresh interrupts later than the pass they start, which was restarted |
| `maxInterruptMicros` | Longest refresh interrupt, in microseconds |
| `lastI2cError` | HAL error bits (`HAL_I2C_ERROR_*`) at the last error callback, NACK (`0x4`) included; a short write or a probe ends in one |
| `stopWithAddrPending` | Transfers whose STOP was handled after the host's next address had matched: the I2C interrupt ran late. Should stay 0 |

```bash
arm-none-eabi-nm build/Debug/DisplayController.elf | grep g_displayStats
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -r32 <address> 0x44
```

`mode=HOTPLUG` attaches without resetting the board.

A stack overflow halts the board in `vApplicationStackOverflowHook()`
(`Core/Src/main.c`), with interrupts disabled and the task's name in
`g_stackOverflowTaskName`, as on the host. The display then freezes on its
current pass.

## Code Layout

| File | Content |
| --- | --- |
| `User/Src/DisplayController.cpp` | The static objects and `DisplayController_Init()` |
| `User/Src/DisplayApp.cpp` | The `DisplayApp` task |
| `User/Src/I2cTarget.cpp` | The I2C target, its message queue and the HAL I2C callbacks |
| `User/Src/FrameAssembler.cpp` | Staging of the attribute messages and their application with the content |
| `User/Src/Screens.cpp` | Self-test, identify and address screens |
| `User/Inc/NoDataTimer.hpp` | Stale-data timeout on the wrapping tick |
| `User/Inc/Stats.hpp` | `g_displayStats` |
| `tests/ScreensTests.cpp`, `tests/NoDataTimerTests.cpp`, `tests/FrameAssemblerTests.cpp` | Native tests |
| `../Common` | Display types, attributes and encoding, the pass sequencing, `PcbDisplayBoard`, `SCT2xxx`, the I2C messages, the address straps, tasks and mutexes |

## Open Items

- Run it on a display board: the interrupt-driven refresh on SPI1/TIM6 with
  DMA, the I2C target against the host including the attribute messages, and
  the boot and test screens.
- Console over USART2 (PA2/PA3); see the README.
