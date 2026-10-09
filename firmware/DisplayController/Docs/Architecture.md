# DisplayController Architecture

How the display board firmware is put together. The display itself (content
types, PCB mapping, multiplexing) is shared with the host and described in
[Display.md](../../Docs/Display.md), the I2C link to the host in
[I2C.md](../../Docs/I2C.md); pins and peripherals are in the
[README](../README.md#hardware).

The first display board runs this firmware on the host's bus at `0x11`.

## Boot

`main()` runs the CubeMX sequence: `HAL_Init()`, `SystemClock_Config()` (HSI,
16 MHz), `MX_GPIO_Init()`, `MX_DMA_Init()`, `MX_SPI1_Init()`, `MX_I2C1_Init()`,
`MX_TIM6_Init()`, `osKernelInitialize()`, `defaultTask`, then
`DisplayController_Init()` from the `RTOS_THREADS` user section, then
`osKernelStart()`.

`DisplayController_Init()` (`User/Src/DisplayController.cpp`), before the
scheduler starts:

1. Creates the `activityLed()` timer for `LED_2`.
2. Shows the first slot-test frame (`Display::slotTestState(0)`) and starts the refresh
   (`PcbDisplayBoard::start()`: enables the SCT outputs and starts TIM6, whose
   interrupt then multiplexes the board with SPI1 DMA transfers, no task
   involved, keeps the board's refresh timeline and drives the `LED_1`
   heartbeat; see
   [Display.md](../../Docs/Display.md#refresh-operation)).
   The test frame is prepared first, so the first frames latched are the
   test, not whatever the drivers held at reset.
3. Starts the `DisplayApp` task and routes the switches to it
   (`Utils::SwitchInput`).
4. Reads the address straps (`Display::detectBoardAddress()`) and stores the
   address in `g_displayStats.address`; `TimelineFollower::init()` notes the
   boot HSITRIM; the status byte starts as "wants syncs" and "needs content"
   (`0xA2`); then the I2C target
   starts on the address (`I2cTarget::begin()`, which also enables the general
   call for the host's timeline sync).

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
| `DisplayApp` | `User/Src/DisplayApp.cpp` | Normal (24) | 2048 (peaks at ~1056 on the board) | Chooses what is shown: boot screens, data with its attributes, "no data", test screens; applies the host's timeline syncs (`TimelineFollower`) |
| `defaultTask` | `Core/Src/main.c` | Normal (24) | 512 | Exits at once (`osThreadExit()`); CubeMX does not allow removing it |

The `LED_1` heartbeat, 20 ms every 2 s, is not a task: the refresh interrupt
writes it at each frame start, in step with the host's. Until the board is
locked to the host's timeline it flashes twice, 200 ms apart
(`TimelineFollower` sets `PcbDisplayBoard::setHeartbeatTwice()`)
([TimelineSync.md](../../Docs/TimelineSync.md#the-timeline-on-every-board)).

`DisplayApp` is a `Task<N>` object with a static stack, and
`Utils::Mutex` uses static storage, so the 3072-byte FreeRTOS heap holds only
`defaultTask`, and only until it exits, just after the scheduler starts. The display refresh runs from TIM6's interrupt, not a task. `configCHECK_FOR_STACK_OVERFLOW` is 2; see
[Diagnostics](#diagnostics).

`DisplayApp` waits on four thread flags, with a 1 s timeout for its periodic
checks:

| Flag | Set by | Meaning |
| --- | --- | --- |
| `kFlagFrame` | `I2cTarget`, from the I2C interrupt | One or more complete 36-byte messages are waiting |
| `kFlagSwitch1` | `SwitchInput`, from the EXTI interrupt | Switch 1 pressed |
| `kFlagSwitch2` | `SwitchInput` | Switch 2 pressed |
| `kFlagSync` | `I2cTarget`, from the I2C interrupt | A timeline sync from the host has arrived, stamped; `TimelineFollower::onSync()` applies it ([TimelineSync.md](../../Docs/TimelineSync.md#following-the-host)) |

Every second it also checks whether the data has gone stale, closes a test
screen that has been up too long, and restarts I2C listening if it has
stopped.

## Screens

`DisplayApp` shows one of these at a time; the contents come from
`User/Src/Screens.cpp`, `../Common/Src/Display/BootScreens.cpp` and
`Display::noDataState()`:

| Screen | Contents | Shown |
| --- | --- | --- |
| Slot test | One `DISPLAYx_EN` slot at a time, everything it drives: digit *n* of every numeric display with its dot (slot 5: L1-L3) and matrix row *n* from the top, so the matrix steps top to bottom (driven by slot 6 - *n*). A dead slot switch shows as a step with its digits unlit | At boot, 200 ms per slot, 1 s in all |
| All segments | Every digit segment with its dot, L1-L3, every matrix dot | Switch 1 |
| Address | `Ad12` (for 0x12) on numeric display 1, `Ad--` if the straps gave no address; the build number on numeric display 2 (`  42`); everything else blank | 2 s at boot, after the slot test; 3 s on switch 2 |
| Identify | Numeric display *n* (0-3) shows *n* + 1 on all four digits (`1111` to `4444`); matrix row *r* lights its first *r* + 1 columns, so the top row has one dot | Switch 1, after all segments |
| Data | The last content from the host, with the blink and level attributes it sent | After a frame arrives, until it goes stale |
| No data | Segment G on the last digit of every numeric display (`   -`), everything else off, matrix blank | Before the first frame, and after 7 h without one |

The test screens and "no data" are plain: full brightness, nothing blinking.
Switch 1 steps through all segments, identify, and back to the data; each test
screen closes by itself after 60 s. A frame that arrives while a test or
address screen is up is kept and shown when it closes. `LED_2` flashes for
20 ms on every write addressed to the board (frames, attributes, the host's
probes), from the address-match interrupt (`I2cTarget::onAddress()`) through
the shared `PulseLed`; writes closer together than that merge into one flash.
The host's sync broadcasts and status reads do not flash it.

The stale-data timeout is 7 hours (`DisplayApp::kNoDataTimeoutMs`), just over
the host's 6-hour refresh interval: the host sends to the remote boards only on
an astro refresh or a `display` command, so a shorter timeout would show "no
data" for most of the day. One missed refresh is therefore enough to show it.
The board then asks for content in its status byte, as it does from boot until
the first content message arrives; the host's next poll, within a minute,
sends the content it holds if that is younger than 7 h
([I2C.md](../../Docs/I2C.md#status-read)). The constant is shared with the host
as `Display::kContentLifetimeMs`.
`NoDataTimer` (`User/Inc/NoDataTimer.hpp`) does the arithmetic on the wrapping
kernel tick.

## I2C Target

`I2cTarget` (`User/Src/I2cTarget.cpp`) listens on I2C1 at the strap address
with the HAL's interrupt-driven sequential listen API, queues each 36-byte
message (four deep) and wakes `DisplayApp`, which decodes it with
`FrameAssembler`: attribute planes are staged and applied with the next
content. It also takes the host's timeline sync on the general-call address,
stamping the board's timeline in the address-match interrupt, and answers
a one-byte read with the status: the timeline locked (`TimelineFollower`) and
content wanted (`DisplayApp`). Listening starts
only once the address is known and is checked every second. The protocol,
both sides of the link and why the I2C interrupt runs at priority 1 are in
[I2C.md](../../Docs/I2C.md#display-board-side).

## Interrupt Priorities

From `DisplayController.ioc` (NVIC) and `stm32g0xx_hal_conf.h`. Lower numbers
pre-empt higher ones; the Cortex-M0+ has four levels, 0 to 3.

| Interrupt | Priority | Why |
| --- | ---: | --- |
| `I2C1_IRQn` (I2C target) | 1 | Must handle a message's STOP before the host's next address matches, about 100 us later; at 3 it could wait behind the refresh interrupt and leave a NACK set ([I2C.md](../../Docs/I2C.md#interrupt-priority)) |
| `TIM6_IRQn` (display refresh) | 3 | Runs the multiplexing, up to about 170 us per pass; latency only shifts a pass, never loses one ([Display.md](../../Docs/Display.md#refresh-operation)) |
| `DMA1_Channel1_IRQn` (SPI1 TX) | 3 | Completes the HAL's SPI state after each pass's 56 us shift; it only has to run before the next pass starts a transfer, at least 480 us later |
| `EXTI4_15_IRQn` (switches) | 3 | Switch presses, only a thread flag |
| `TIM1_BRK_UP_TRG_COM_IRQn` (HAL tick) | 3 | `TICK_INT_PRIORITY`; HAL timeouts tolerate the delay |
| SysTick, PendSV | 3 | FreeRTOS: the lowest level, as the port requires |

Nothing is at 0 or 2. Interrupts at the same level do not pre-empt each other,
so a level-3 handler waits for the refresh interrupt to finish.

## Diagnostics

Until the board has a console, its counters are read over SWD. They are in
`g_displayStats` (`User/Inc/Stats.hpp`), a C-linkage struct of 30 32-bit
fields, 0x78 bytes. Signed values are stored as two's complement:

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
| `syncsReceived` | Sync broadcasts received and stamped, in the interrupt |
| `syncsShort` | General-call writes that ended before 7 bytes |
| `syncsRejected` | Sync broadcasts that did not decode |
| `syncsApplied` | Syncs decided on by `TimelineFollower` |
| `syncJumps` | Of those, jumps: the first sync, or one more than 50 ms plus 2.5 % of the interval out |
| `syncLocked` | 1 while the board tells the host it needs no burst: a sync has measured its rate since the last jump or HSITRIM step |
| `syncErrorMicros` | Host minus board at the last sync, before any jump, signed |
| `syncDriftMicros` | Drift over the last interval long enough to measure the rate, signed |
| `syncRatePpm` | The servo's rate, this board's clock against the host's, signed |
| `hsiTrim` | `RCC_ICSCR.HSITRIM` now |
| `hsiChanges` | HSITRIM steps taken |
| `syncDataMicros` | The last sync's seven data bytes, from the address match, where the board stamps, to the receive-complete interrupt: 630 us at 100 kHz plus whatever held the host up between them |
| `syncDataMaxMicros` | The longest of those |

```bash
arm-none-eabi-nm build/Debug/DisplayController.elf | grep g_displayStats
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -r32 <address> 0x78
```

`mode=HOTPLUG` attaches without resetting the board. `tools/stats_log.py`
reads the struct on a loop and prints a line per sync
([TimelineSync.md](../../Docs/TimelineSync.md#measuring)).

A stack overflow halts the board in `vApplicationStackOverflowHook()`
(`Core/Src/main.c`), with interrupts disabled and the task's name in
`g_stackOverflowTaskName`, as on the host. The display then freezes on its
current pass.

## Code Layout

| File | Content |
| --- | --- |
| `User/Src/DisplayController.cpp` | The static objects and `DisplayController_Init()` |
| `User/Src/DisplayApp.cpp` | The `DisplayApp` task |
| `User/Src/I2cTarget.cpp` | The I2C target, its message queue, the sync buffer and the HAL I2C callbacks |
| `User/Src/TimelineFollower.cpp` | Applies the host's timeline syncs and moves HSITRIM |
| `User/Src/FrameAssembler.cpp` | Staging of the attribute messages and their application with the content |
| `User/Src/Screens.cpp` | Self-test, identify and address screens |
| `User/Inc/NoDataTimer.hpp` | Stale-data timeout on the wrapping tick |
| `User/Inc/Stats.hpp` | `g_displayStats` |
| `tools/stats_log.py` | Logs the timeline sync fields of `g_displayStats` over SWD ([TimelineSync.md](../../Docs/TimelineSync.md#measuring)) |
| `tests/ScreensTests.cpp`, `tests/NoDataTimerTests.cpp`, `tests/FrameAssemblerTests.cpp` | Native tests |
| `../Common` | Display types, attributes and encoding, the pass sequencing, `PcbDisplayBoard`, the timeline and its sync decisions, `SCT2xxx`, `HsiTrim`, the I2C messages, the address straps, tasks and mutexes; see [firmware/Docs](../../Docs/) |

## Open Items

- Not yet checked on the board: rejecting unknown commands and short writes,
  recovery from bus errors, the 7-hour "no data" timeout and the switch test
  screens.
- The timeline sync: a run over a day's temperature, and the board's own HSI16
  moving about 100 ppm within minutes, which sets the error now that the host
  runs from a crystal
  ([TimelineSync.md](../../Docs/TimelineSync.md#open-items)).
- Console over USART2 (`PA2`/`PA3`); see the [README](../README.md#features).
