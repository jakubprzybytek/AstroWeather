# Display

The LED display as both firmware images drive it: the content and attribute
model, the PCB encoding and the interrupt-driven multiplexing. All of it is in
[`../Common`](../Common/README.md) and compiled into the
[HostController](../HostControllerA/README.md) and the
[DisplayController](../DisplayController/README.md), including the refresh
timeline the display boards keep in step with the host's
([Timeline Sync](TimelineSync.md)). How the host spreads the forecast over
several boards is in the host's
[Display.md](../HostControllerA/Docs/Display.md); how boards talk to each
other is in [I2C.md](I2C.md).

## Hardware

Every board, host or display board, has the same display hardware:

- Four multiplexed four-digit, seven-segment numeric displays, each with three
  indicator dots: L1 and L2 form the colon used for time, L3 is the apostrophe
  before the last digit.
- One 5x21 dot matrix.
- SCT2xxx LED drivers in one SPI daisy chain, with a latch (`SCT_LATCH`) and an
  output enable (`SCT_ENABLE`, active low).
- Five active-low multiplexing outputs, `DISPLAY_1_EN` through `DISPLAY_5_EN`.

| | HostController | DisplayController |
| --- | --- | --- |
| MCU | STM32G0B1 | STM32G070 |
| SCT chain | SPI3, `SPI3_TX` on DMA1 channel 4 | SPI1, `SPI1_TX` on DMA1 channel 1 |
| Refresh timer | TIM2 | TIM6 |
| Pins | `PB3` SCK, `PB5` MOSI, `PB6` latch, `PB7` enable, `PD3` `PA15` `PD1` `PD2` `PD0` slot selects | the same |

The pin labels are the same in both CubeMX projects, so the shared code
compiles unchanged; each project passes its own SPI and timer handles in.

## Code

| Unit | Contents |
| --- | --- |
| `DisplayTypes` | `LogicalBoardState` (the content), `BoardAttributes` (blink and level planes), `NumericDisplay` and `MatrixRow` setters, `noDataState()` |
| `DisplayBoard` | The board interface: content, attributes, `submit()`, `show()`, status hooks |
| `DisplayCodec` | Encoding of a logical state into SPI-ready slot frames, per pass and blink phase |
| `RefreshSequencer` | The pass table, pass order and blink phase, the frame numbers and the per-frame length adjustment |
| `PcbDisplayBoard` | A `DisplayBoard` that multiplexes its own LEDs from the refresh timer's interrupt and keeps the board's timeline |
| `Timeline` | The timeline's constants, the `LED_1` heartbeat, `TimelineStamp` and `positionMicros()`; see [Timeline Sync](TimelineSync.md) |
| `TimelineServo`, `TimelineSync` | A display board's per-frame correction and its decisions on each sync from the host |
| `BootScreens` | The slot test and address screens every board shows at power-up |
| `DisplayAddress` | The address straps; see [I2C.md](I2C.md#addresses-and-straps) |
| `DisplayI2cProtocol` | The I2C messages; see [I2C.md](I2C.md#messages) |
| `SCT2xxx` (`Device/`) | The LED driver chain over SPI |
| `HsiTrim` (`Device/`) | The HSI16 trim both boards' timelines run on; see [Utilities.md](Utilities.md#hsi-trim) |

The host adds `BufferedDisplayBoard`, a `DisplayBoard` that forwards its state
over I2C, and the `Display` aggregate of all boards; see its
[Display.md](../HostControllerA/Docs/Display.md).

## Public Interface

Each board exposes four numeric displays indexed `0` to `3` and five matrix
rows indexed `0` to `4`; row `0` is the top.

```cpp
numeric(i).setFixed(int16_t mantissa, uint8_t precision = 0);
numeric(i).setValue(int16_t value);
numeric(i).setValue(float value, uint8_t precision = 0);
numeric(i).setTime(uint8_t hour, uint8_t minute);
numeric(i).setTimeUnset();
numeric(i).setBlank();
numeric(i).setSegments(NumericSegments{...});

matrix(row).setRow(uint32_t columns);   // bit 0 = column 1 ... bit 20 = column 21

attributes().setNumericBlink(i, NumericSegments{...});
attributes().setMatrixBlink(row, uint32_t columns);
attributes().setNumericLevel(i, level);                        // whole display
attributes().setNumericLevel(i, NumericSegments{...}, level);  // some segments
attributes().setMatrixLevel(row, uint32_t columns, level);
attributes().clearBlink();  attributes().clearLevels();

board.submit();   // show the content with its attributes
board.show(state, attributes);  // show a one-off frame, leaving the board's own state alone
```

Setters change the board's buffered state only; `submit()` shows it. Setters
are not synchronized: concurrent writers are last-writer-wins, and the
`submit()` callers serialize the transfers. All setters convert their input to
the canonical representation below before storing it. Only the lowest 21 bits
of `setRow()` are used.

`show()` exists for the boot screens; a board that only forwards over I2C
ignores it. `present()`, `address()`, `lastSubmitOk()`, `refreshStats()` and
`setPassPercent()` serve status reporting and tuning, and only the boards that
have them override the defaults.

### Blink and brightness levels

Every lit segment and pixel has two attributes, stored in `BoardAttributes`
next to the content and in the same layout, as three bit planes: `blink`, and
`level0`/`level1`, the two bits of a brightness level 0 to 3. The default is
nothing blinking and every element at level 3, full. An attribute on an unlit
element has no effect, and attributes persist across content updates: the
host's clock sets its colon to blink once and keeps calling `setTime()`.

- **Blink** is on/off: a blinking element is lit for half a second and dark for
  half a second (`RefreshSequencer::kBlinkHalfPeriodFrames`, 25 frames at
  50 Hz). The phase follows the frame number, frames 1-25 on and 26-50 off, so
  boards on the host's timeline blink together
  ([Timeline Sync](TimelineSync.md)); a display board blinks on its own phase
  only from its boot until the first sync arrives.
- **Levels** are made in time. Each multiplexing slot is shown as four passes of
  12, 31, 27 and 30 % of its 4 ms, and a level lights the passes in
  `Display::kLevelPasses`: level 1 the first, level 2 the second, level 3 all
  four. The matrix LEDs are brighter than the numeric ones, so the matrix sits
  out the last pass (`Display::kMatrixPasses`).

| Level | Numeric segment | Matrix pixel |
| --- | ---: | ---: |
| 0 | off | off |
| 1 | 12 % | 12 % |
| 2 | 31 % | 31 % |
| 3 (default) | 100 % | 70 % |

The percentages were chosen by eye for even-looking steps at normal
brightness; perceived brightness is roughly logarithmic, so equal steps of
light would look uneven. The pass lengths are the one tunable
(`setPassPercent()`, on the host `display passes <a> <b> <c> <d>`, not saved);
the level-to-pass table is fixed. Levels multiply with the analog brightness
([Low brightness](#low-brightness)), so level 1 in low brightness in a dark room
can be near invisible.

## Numeric Representation

Each numeric display is five normalized segment bytes, one per multiplexing
slot:

```cpp
struct NumericSegments {
    std::array<uint8_t, 5> slots;
};
```

Slots 0 to 3 are the four digits, with this bit layout:

| Bit | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Segment | A | B | C | D | E | F | G | DP |

Slot 4 is the indicator position: its A, B and C bits are L1, L2 and L3. L3 is
not used by any setter. `setSegments()` takes raw masks in this logical layout,
never the PCB bit positions.

Patterns with a meaning:

| Pattern | Segments | Use |
| --- | --- | --- |
| Error | D on all four digits (`____`) | A setter got a value it cannot show |
| Unavailable | DP on all four digits (`....`) | The forecast has no value (`?` in the payload) |
| Time unset | G on all four digits, L1 and L2 (`--:--`) | `setTimeUnset()`: the clock is not set |
| No data | G on the last digit only (`   -`) | `setNoData()`: no data received yet |

### Fixed-point values

The displayed value is `mantissa / 10^precision`. Digits are right-aligned;
leading zeroes are blank except the digit left of the decimal point, so a value
below one keeps its `0.`. A minus sign goes immediately left of the first shown
digit. The decimal point is the DP segment of the digit before it, not a
position of its own.

| Call | Display |
| --- | --- |
| `setFixed(1234, 0)` | `1234` |
| `setFixed(1234, 1)` | `123.4` |
| `setFixed(1234, 3)` | `1.234` |
| `setFixed(-999, 1)` | `-99.9` |
| `setFixed(42, 0)` | `42`, in the two rightmost positions |
| `setFixed(5, 1)` | `0.5` |
| `setFixed(-5, 1)` | `-0.5` |
| `setFixed(-1, 2)` | `-0.01` |
| `setFixed(1, 3)` | `0.001` |
| `setFixed(-1, 3)` | error pattern |

The four positions hold the shown digits, at least `precision + 1` of them,
plus the minus sign. A value needing more than four positions shows the error
pattern; that includes every negative value at precision 3. The `float`
overload rounds `value * 10^precision` to an integer and then follows the same
rules; NaN, infinity and values that do not fit give the error pattern. Floats
are never stored or sent over I2C.

### Time, blank and no data

`setTime(hour, minute)` takes 00 to 99 for each, unchecked against 23 or 59,
and lights L1 and L2. The hour's leading zero is blank: `setTime(3, 7)` shows
` 3:07`. A value above 99 gives the error pattern. `setTimeUnset()` shows
`--:--`, `setBlank()` clears all five slots.

`setNoData()` shows `   -`, so it cannot be mistaken for `--:--` or the error
pattern, and `noDataState()` is a whole board in that state: every numeric
display `   -`, the matrix blank. Every board shows it after its boot screens
until data arrives; a display board also returns to it after 7 hours without a
frame ([DisplayController Architecture](../DisplayController/Docs/Architecture.md#screens)).

## Boot Screens

Every board, the host's own included, runs `showBootScreens()`
(`BootScreens.cpp`) once after power-up, about 3 s, through `show()`, so its
own state is untouched and shows afterwards:

1. **Slot test**: each `DISPLAYx_EN` slot lit on its own for 200 ms (1 s in
   all), with everything that slot drives: digit *n* of every numeric display
   with its dot (slot 5: L1-L3), and matrix row *n* counted from the top, so
   the matrix steps top to bottom (that row is driven by slot 6 - *n*). A dead
   slot switch shows as a step with its digits unlit.
2. **Address and build**: `Ad12` (for 0x12) on numeric display 1 for 2 s,
   from the board's straps, `Ad--` without a valid address; the firmware's
   build number on numeric display 2, right-aligned, its last four digits
   above 9999 ([Development.md](Development.md#build)).

Both are plain: full brightness, nothing blinking. On the host
`MainLoopTask` runs them; on a display board `DisplayApp` does.

## Logical Board Buffer

A board's content is 35 bytes, and each attribute plane uses the same layout.
This is what travels over I2C:

| Offset | Size | Content |
| ---: | ---: | --- |
| 0 | 5 | Numeric display 1: five segment slots |
| 5 | 5 | Numeric display 2 |
| 10 | 15 | Matrix rows 0-4, three bytes per 21-bit row, little-endian |
| 25 | 5 | Numeric display 3 |
| 30 | 5 | Numeric display 4 |

Numeric displays 1 to 4 here, and in the wiring tables below, are indices 0
to 3 of the interface. Matrix bits 21 to 23 are always zero.

## PCB Encoding

`DisplayCodec::encodePcb()` turns a logical state into a frame of five slots of
seven SPI bytes. `encodePasses()` runs it eight times, once per blink phase and
pass, on the elements that phase and pass show (`passElements()`: the content
masked by the level planes, the blink plane and the matrix's pass mask), giving
the 280-byte `PassFrames` the refresh reads.

Each slot's seven bytes, in daisy-chain order from the first device:

1. Numeric display 1
2. Numeric display 2
3. Matrix byte 1 (columns 1-8)
4. Matrix byte 2 (columns 9-16)
5. Matrix byte 3 (columns 17-21; bits 21-23 zero)
6. Numeric display 3
7. Numeric display 4

The last device must be shifted first, so the bytes go out in the reverse
order, numeric display 4 first. SPI is MSB first, so no bit is reversed inside
a byte. An SCT output bit of 1 lights its LED.

### Numeric segment wiring

| Segment | Display 1 bit | Display 2 bit | Display 3 bit | Display 4 bit |
| --- | ---: | ---: | ---: | ---: |
| A | 2 | 5 | 0 | 0 |
| B | 1 | 6 | 1 | 1 |
| C | 4 | 1 | 3 | 7 |
| D | 3 | 3 | 6 | 4 |
| E | 6 | 2 | 5 | 5 |
| F | 0 | 7 | 2 | 2 |
| G | 7 | 4 | 7 | 3 |
| DP | 5 | 0 | 4 | 6 |

### Multiplexing

- `DISPLAY_1_EN` to `DISPLAY_4_EN` select digit positions 1 to 4 on every
  numeric display; `DISPLAY_5_EN` selects the indicator position, where only
  L1, L2 and L3 exist. L1, L2 and L3 use the segment A, B and C bits of their
  display.
- Each slot also drives one matrix row. Hardware rows count from the bottom:
  `DISPLAY_1_EN` drives the bottom row, logical row 4, and `DISPLAY_5_EN` the
  top row, logical row 0. `encodePcb()` reverses the order (slot *n* carries
  logical row 4 - *n*).
- All `DISPLAY_x_EN` outputs are active-low.
- Matrix columns 1 to 21 are SCT bits 0 to 20: columns 1-16 on `U505` outputs
  0-15, columns 17-21 on `U510` outputs 0-4. Known faults around `U505` on the
  prototype are in [Display-Issues.md](../../KiCad/Docs/Display-Issues.md#matrix-column-faults-around-u505).

## Refresh Operation

`PcbDisplayBoard` multiplexes the board from the refresh timer's update
interrupt, with the SPI transfers done by DMA; no task is involved. A frame is
five slots of 4 ms, 50 Hz. Each slot is shown as four passes
(`RefreshSequencer`), longest first, so the slot switch and its settle time
come out of the longest pass: with the default table 31, 30, 27 and 12 % of
the slot, 1240, 1200, 1080 and 480 us.

The interrupt runs once per pass:

1. **Latch** the data the DMA shifted in during the pass that has just ended.
   At the first pass of a slot this is the slot switch: blank the outputs
   (`SCT_ENABLE` high), turn off the previous `DISPLAY_x_EN`, pulse the latch,
   turn on the new one, wait `kSlotSettleMicros` (10 us, timed from SysTick)
   and re-enable the outputs. Within a slot it is only the latch pulse: the SCT
   drivers keep their outputs while the latch is low, so the shift does not
   disturb the display. If the previous shift is still running, the interrupt
   was later than a whole pass; the old data stays and `lateShifts` counts it.
2. **Set the length** of the pass that starts now by writing ARR (auto-reload
   preload is off, so it applies to the running period). If the interrupt came
   later than the pass is long, the counter is restarted rather than left to
   run to the timer's full range, and `lateInterrupts` counts it. At the first
   pass of a frame the interrupt first starts the frame on the timeline
   (`startFrame()`): it takes the frame's length correction from the servo,
   sets `LED_1` for the heartbeat and records when the frame started; see
   [Timeline Sync](TimelineSync.md).
3. **Start the DMA** that shifts the next pass's seven bytes (56 us at 1 MHz)
   while this pass is lit. At a frame boundary a pending submission is swapped
   in first, so a frame never mixes two submissions.

The display is dark only for the slot swap, about 12 us per slot. The pass
timing depends on interrupt latency alone: 13 us typically, up to about 260 us
under WiFi load on the host, against the 480 us shortest pass. The settle time
lets the old slot's high-side P-MOSFET (Si2333DDS, turned off only by its gate
pull-up) turn off before the outputs return; too short shows a faint copy of
the new slot on the old one. If that ghosting is visible, raise
`kSlotSettleMicros` in `PcbDisplayBoard.cpp`.

`submit()` encodes the content and attributes into the back frame set
(`encodePasses()`, about 1.5 ms at `-O0`, in the caller's task) and asks for a
swap; a submission not yet shown is overwritten by a newer one.
`PcbDisplayBoard::submitMutex_` allows one encode at a time; the interrupt takes
no lock.

The interrupt takes up to about 170 us at `-O0`, 1000 times a second: 2-3 % of
the CPU, and that long a hold-off for every other interrupt of the same
priority. The refresh timer and the display DMA are at priority 3; anything
that cannot wait that long runs above them, see the interrupt priorities of the
[HostController](../HostControllerA/Docs/Architecture.md#interrupt-priorities)
and the
[DisplayController](../DisplayController/Docs/Architecture.md#interrupt-priorities).
`refreshStats()` reports frames shown (50 a second), late shifts, late
interrupts and the longest interrupt: on the host in `status`, on a display
board in `g_displayStats`.

### Timer and DMA configuration

Set in each `.ioc`:

| Setting | HostController | DisplayController |
| --- | --- | --- |
| Refresh timer | TIM2: prescaler 15 (1 MHz from 16 MHz HSI), period 3999 (a placeholder, rewritten every pass), up-counting, auto-reload preload off, update interrupt at priority 3 | TIM6: the same |
| Display SPI | SPI3, master TX only, prescaler 16 (1 MHz), MSB first | SPI1, the same |
| Display DMA | `SPI3_TX` on DMA1 channel 4, memory to peripheral, byte/byte, memory increment, normal mode, priority high; interrupt `DMA1_Ch4_7_DMA2_Ch1_5_DMAMUX1_OVR` at priority 3 | `SPI1_TX` on DMA1 channel 1, the same; interrupt `DMA1_Channel1` at priority 3 |

The DMA interrupt must stay enabled: `HAL_SPI_Transmit_DMA()` finishes its
state machine in the transfer-complete interrupt and refuses the next transfer
as busy without it. Auto-reload preload must stay off. The SCT2024 accepts up
to 25 MHz; above about 4 MHz the SCK and MOSI pins would need a faster GPIO
speed than `GPIO_SPEED_FREQ_LOW`.

`HAL_TIM_PeriodElapsedCallback()` in each `main.c` forwards the timer to
`Display_PcbTimerElapsed()` from its user-code section. TIM1 is the HAL time
base and stays separate. `PcbDisplayBoard::start()` primes the timer, enables
the SCT outputs and starts it. At power-up `SCT_ENABLE` and the slot selects
start high (CubeMX), so nothing lights before the first frame.

## Timeline Sync

Every board's refresh frames form a timeline of 20 ms frames numbered from the
host's boot, and the display boards keep theirs on the host's, so the blink
phase and the `LED_1` heartbeat, which both run off the frame number, are in
step on every board. The timeline, the host's schedule of syncs, how a board
follows them, HSITRIM and how to measure it are in
[TimelineSync.md](TimelineSync.md).

## Low Brightness

LED brightness is analog. The light-sensor divider on the host sets
`LED_BRIGHTNESS`, bussed to every board on pin 10 of `J102`/`J104`. On each
board one current-set stage per driver (`MCP6006` + `BC847`, `U503`/`Q506` and
the rest) turns that voltage into the SCT `REXT` current. The firmware sets no
brightness value; its one lever is `LOW_POWER_ENABLE` (`PB8`, pin 9 of the same
connectors), which switches on `Q701`, shorts `R705` at the bottom of the
divider and lowers `LED_BRIGHTNESS` by a fixed ratio, from about 1.17 V to
0.49 V in bright light. The level still follows the ambient light.

Only the host drives the pin; how is in its
[Display.md](../HostControllerA/Docs/Display.md#low-brightness). The net is
bussed, so the DisplayController leaves its own `PB8` in analog mode and never
drives it ([Hardware review](../../KiCad/Docs/Hardware_Review.md) M-4).

## Tests

Native suites in `../Common/tests` (see [Testing.md](Testing.md)):

| Suite | Covers |
| --- | --- |
| `NumericDisplayTests.cpp` | `setFixed()` including values below one and values that do not fit, both `setValue()` overloads, `setTime()`, `setTimeUnset()`, `setBlank()`, `setSegments()`, `setNoData()`, `noDataState()` |
| `DisplayCodecTests.cpp` | Golden vectors from the wiring tables: every segment of every digit on its bit, byte and slot, the indicators, the 21 matrix columns and bits 21-23, the order of the matrix rows |
| `DisplayAttributesTests.cpp` | Attribute defaults, the blink and level setters with their masks, clamping and out-of-range indices, the plane operations |
| `DisplayPassesTests.cpp` | The pass table's invariants (sums to 100, the level percentages, the matrix's 70 %), which elements each pass and blink phase show, `encodePasses()` against `encodePcb()` |
| `RefreshSequencerTests.cpp` | Pass lengths and their validation, the longest-first order, slot and frame boundaries, the blink phase, the frame adjustment's split over the slots, renumbering the frames |
| `TimelineSyncTests.cpp` | The servo's rate and slew, the jump, rate and HSITRIM decisions, `HsiTrim::allowedSteps()`, the heartbeat frame, `positionMicros()`, and the simulated board against the host's schedule ([TimelineSync.md](TimelineSync.md#accuracy)) |
| `BootScreensTests.cpp` | The slot-test and address states and the boot sequence |

The I2C message and address-strap suites are listed in
[I2C.md](I2C.md#tests), the host's sync schedule in its
[Testing.md](../HostControllerA/Docs/Testing.md#coverage-by-module). Not
covered natively: the refresh interrupt itself, `stampNow()`, `applySync()`
and `TimelineFollower`, which are checked on the boards through the refresh
and sync counters.
