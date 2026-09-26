# Display Functionality

## Overview

The system contains one Host Controller board and up to five Display Controller boards. Every board has the same physical display hardware:

- Four multiplexed four-digit, seven-segment numeric displays.
- One 5x21 dot-matrix display.
- Three special indicator dots.
- SCT2xxx LED drivers connected as one SPI daisy chain.
- Five active-low multiplexing outputs, `DISPLAY_1_EN` through `DISPLAY_5_EN`.

The Host Controller fetches application data, displays its local portion, and sends the remaining board values to Display Controllers over I2C. A Display Controller receives logical display values over I2C and renders them on its local PCB; see [Display Controller](#display-controller).

## Software Architecture

### Display

`Display` exists only in the Host Controller firmware. It is the logical representation of the complete multi-board display and owns a collection of board interfaces:

- One local PCB-backed Display Board.
- Five remote buffer-backed Display Boards, one for each Display Controller.

Client code accesses all boards through the same Display Board interface without needing to know whether a board is local or remote. `Display::submit()` submits the local board's logical buffer for PCB encoding and periodic SPI refresh, then sends each remote board's logical buffer to its configured I2C address. It serializes SPI/I2C transfer sequences; setters remain unsynchronized, so concurrent clients intentionally use last-writer-wins pending state.

### PCB-backed Display Board

The PCB-backed implementation:

- Owns the SCT2xxx SPI interface, latch and enable signals, and the five multiplexing GPIOs.
- Stores logical display values and their attributes (blink, level).
- Converts them into PCB-specific segment and matrix bit mappings, one prepared frame per blink phase and pass.
- Runs the local multiplexing from the refresh timer's interrupt, with the SPI transfers done by DMA; no task is involved. See [Refresh Operation](#refresh-operation).

The HostController `AstroWeather.cpp` creates this object and starts it. `start()` primes TIM2 and calls `HAL_TIM_Base_Start_IT(&htim2)`. The DisplayController creates one too, on SPI1 and TIM6.

### Buffer-backed Display Board

The buffer-backed implementation exists only on the Host Controller. It stores logical board values and attributes and does not apply PCB wiring mappings. `submit()` sends them to a Display Controller as four I2C messages, the attributes first and then the content, where the PCB-backed implementation performs the mapping; see [I2C Transport](#i2c-transport).

### Code Ownership

The display code is split between the shared `../Common` tree, compiled into both the host and the DisplayController, and this project's `User`:

- `../Common/Src/Device` holds the SCT2xxx driver.
- `../Common/Src/Display` holds the logical content types and attributes, the PCB encoding of content and passes, the pass sequencing (`RefreshSequencer`), the multiplexing refresh (`PcbDisplayBoard`), the I2C message format and the address straps.
- `User/Src/Display` holds the host-only parts: the aggregate `Display`, the buffer-backed remote boards (`BufferedDisplayBoard`) and `LowBrightness`. `User/Src/Device` holds `I2cBus` and the EEPROM driver.

## Public Interface

Each board exposes four numeric displays indexed from `0` through `3` and five dot-matrix rows indexed from `0` through `4`. Matrix row `0` is the top row and matrix row `4` is the bottom row.

Each of the four numeric displays has its own three special indicators: L1 and L2 form the double dots used for time, and L3 is the apostrophe before the last digit. `DISPLAY_1_EN` through `DISPLAY_4_EN` select the four numeric digit positions on every numeric display. `DISPLAY_5_EN` selects the special-indicator position on every numeric display; only the three special-indicator segments are used in this position.

The interface is:

```cpp
numeric[i].setFixed(int16_t mantissa, uint8_t precision = 0);
numeric[i].setValue(int16_t value);
numeric[i].setValue(float value, uint8_t precision = 0);
numeric[i].setTime(uint8_t hour, uint8_t minute);
numeric[i].setTimeUnset();
numeric[i].setBlank();
numeric[i].setSegments(NumericSegments{...});

matrix[row].setRow(uint32_t columns);

// Attributes, kept next to the content and shown with it; see below.
attributes().setNumericBlink(i, NumericSegments{...});
attributes().setMatrixBlink(row, uint32_t columns);
attributes().setNumericLevel(i, level);                      // whole display
attributes().setNumericLevel(i, NumericSegments{...}, level);  // some segments
attributes().setMatrixLevel(row, uint32_t columns, level);
attributes().clearBlink();  attributes().clearLevels();

display.submit();       // local board, then every remote board over I2C
display.submitLocal();  // local board only, no I2C traffic
```

`submitLocal()` takes the same lock as `submit()`. It is for frequent changes that touch only the local board, such as the current-sense readout, the clock and the refresh progress bar.

Only the lowest 21 bits passed to `setRow()` are used. Bit 0 drives matrix column 1 and bit 20 drives matrix column 21.

Additional integer overloads may be provided. All setters convert their input to the canonical logical representation before it is stored.

### Blink and brightness levels

Every lit segment and pixel has two attributes, stored in `Display::BoardAttributes` next to the content in the same layout, as three bit-planes: `blink`, and `level0`/`level1`, the two bits of a brightness level 0 to 3. The default is nothing blinking and everything at level 3, full. An attribute on an unlit element has no effect, and attributes persist across content updates, so the clock sets its colon to blink once and keeps calling `setTime()`. Like the content, they are last-writer-wins: an astro refresh resets the numerics' attributes of every board it draws (the clock re-applies its colon on every redraw) and sets the matrix rows' from the payload.

- **Blink** is on/off: a blinking element is shown in the on half-period and dark in the off one, 0.5 s each at the 50 Hz frame rate (`RefreshSequencer::kBlinkHalfPeriodFrames`). The phase is the board's own and free-running; boards drift apart within a minute, which does not matter as long as no blinking element spans boards.
- **Levels** are made in time. Each multiplexing slot is shown as four passes of 12, 39, 19 and 30 % of its 4 ms, and a level lights the passes in `Display::kLevelPasses`: level 1 the first, level 2 the second, level 3 all four. A numeric segment at levels 1, 2 and 3 is therefore lit for 12, 39 and 100 % of the slot, percentages chosen by eye for even-looking steps (brightness perception is roughly logarithmic, so equal steps of light would look uneven). The matrix LEDs are visibly brighter than the numeric ones, so the matrix sits out the last pass (`Display::kMatrixPasses`) and its levels get 12, 39 and 70 %.

| Level | Numeric segment | Matrix pixel |
| --- | ---: | ---: |
| 0 | off | off |
| 1 | 12 % | 12 % |
| 2 | 39 % | 39 % |
| 3 (default) | 100 % | 70 % |

The pass lengths are the one tunable: `display passes <a> <b> <c> <d>` changes them at run time, to judge a curve by eye; the encoder's level-to-pass table is fixed. The levels multiply with the analog brightness (the light sensor and `display low`), so level 1 in low brightness in a dark room may be near invisible; the table above was tuned at normal brightness. See [Refresh Operation](#refresh-operation) for how the passes are shown.

`setSegments(const NumericSegments&)` accepts five normalized A-G/DP masks: the
first four control visible digits and the fifth controls special indicators.
Raw segment input uses logical segment masks, not PCB-specific encoded bit
positions. Application code represents unavailable numeric API data with the
decimal-point segment in each of the first four slots and zero in the fifth,
producing four dots. This is distinct from the normal validation error pattern,
which uses segment D in the first four slots to produce four underscores.

### Allocation on the Host Controller

The astro refresh writes all four numeric displays and matrix rows 0-3 of every
board; see [AstroRefresh.md](AstroRefresh.md#display-mapping). On the local
board, other clients share some of them:

| Local element | Other client | Default |
| --- | --- | --- |
| Numeric 2 | Current sense, in mA, ten times a second; see [CurrentSense.md](CurrentSense.md) | on (`adc display`) |
| Numeric 3 | Clock, `HH:MM`, redrawn each minute; see [RTC.md](RTC.md) | on (`time display`) |
| Matrix row 4 | Astro refresh progress bar | always |

With the defaults, the forecast's maximum and minimum temperatures for night 0
are therefore overwritten on the local board: numeric 2 within 100 ms, numeric
3 at the next minute. The `display` console commands, for testing, write local
numeric displays and matrix rows and then call `submit()`. There is no field
ownership; the last writer wins.

## Numeric Representation

Each numeric display is stored as five normalized segment bytes, one for each multiplexing slot:

```cpp
struct NumericSegments {
    std::array<uint8_t, 5> slots;
};
```

For slots 0 through 3, each byte uses this normalized bit layout:

| Bit | Segment |
|---:|---|
| 0 | A |
| 1 | B |
| 2 | C |
| 3 | D |
| 4 | E |
| 5 | F |
| 6 | G |
| 7 | DP |

Slot 4 is the special-indicator position. Its normalized A, B, and C bits represent L1, L2, and L3. The remaining bits are unused for the current hardware. `setSegments()` can be used for custom glyphs and other non-numeric content.

### Fixed-point Values

For values set through the numeric convenience API, the displayed mathematical value is:

```text
mantissa / 10^precision
```

The digits are right-aligned. Leading zeroes are blank, except the digit immediately left of the decimal point, which is always shown, so a magnitude below one keeps its `0.`. A negative value's minus sign goes in the position immediately left of its first shown digit. The resulting segments are stored directly in the normalized slot bytes.

Examples:

| Method | Stored mantissa | Precision | Display |
|---|---:|---:|---|
| `setFixed(1234, 0)` | 1234 | 0 | `1234` |
| `setFixed(1234, 1)` | 1234 | 1 | `123.4` |
| `setFixed(1234, 2)` | 1234 | 2 | `12.34` |
| `setFixed(1234, 3)` | 1234 | 3 | `1.234` |
| `setFixed(-999, 0)` | -999 | 0 | `-999` |
| `setFixed(-999, 1)` | -999 | 1 | `-99.9` |
| `setFixed(42, 0)` | 42 | 0 | `42`, in the two rightmost positions |
| `setFixed(5, 1)` | 5 | 1 | `0.5` |
| `setFixed(-5, 1)` | -5 | 1 | `-0.5` |
| `setFixed(-1, 2)` | -1 | 2 | `-0.01` |
| `setFixed(1, 3)` | 1 | 3 | `0.001` |
| `setFixed(-1, 3)` | -1 | 3 | error pattern |

The decimal point does not take a display position of its own; it is the DP segment of the digit before it. The four positions therefore hold the shown digits plus, for a negative value, the minus sign. The shown digits are all significant digits of the mantissa, and at least `precision + 1` of them. Values that need more than four positions are invalid and give the error pattern. That makes every negative value with precision 3 invalid, because `-0.001` already needs five positions; use precision 2 or less for negative values.

### Float Input

The float overload is an input convenience only. It converts the input to fixed point by rounding `value * 10^precision` to the nearest integer, validates the result, and stores only normalized segment bytes. Float values are never stored in the refresh state or transmitted over I2C.

The conversion must reject NaN, infinity, unsupported precision, and values that do not fit the display. Integer and fixed-point overloads are preferred when exact decimal behavior matters.

### Time and Blank Modes

`setTime(hour, minute)` accepts hours and minutes from `00` through `99` and stores four normalized digit slots plus L1 and L2 in the indicator slot:

```text
slots[0..3] = HHMM
slots[4].A = L1 = enabled
slots[4].B = L2 = enabled
```

The hour's leading zero is blank; the minutes always have two digits. For example, `setTime(3, 7)` displays ` 3:07` and `setTime(23, 7)` displays `23:07`. An hour or minute above 99 gives the error pattern. Neither is checked against 23 or 59.

`setTimeUnset()` shows `--:--`: segment G on all four digits, with L1 and L2 lit. The clock uses it until the time has been set.

`setBlank()` clears all five slot bytes.

### No Data

`setNoData()` lights segment G on the last digit only (`   -`), with every other digit, dot and indicator off, so it cannot be mistaken for the unset clock `--:--` or the error pattern. `Display::noDataState()` is a whole board in this state: every numeric display `   -` and the matrix blank.

Every board starts in it:

- The host's local board shows it from boot until the first astro refresh. The current readout (numeric display 2) and the clock (display 3) take over their displays straight away, so in practice displays 0 and 1 and the matrix show it until the first refresh.
- A Display Controller shows it after its boot screens until the first frame from the host, and again when no frame has arrived for 7 hours. The host sends to the remote boards only on an astro refresh or a `display` command, so the timeout is just over the 6-hour refresh interval; one missed refresh is enough to show it. See [DisplayController Architecture](../../DisplayController/docs/Architecture.md#screens).

If a setter receives an invalid value, it stores the error pattern: segment D enabled in each of the four digit slots and the indicator slot blank.

## Logical Board Buffer

A board's transport-level logical buffer contains 35 bytes. The three attribute planes use the same layout, one message each:

| Offset | Size | Content |
|---:|---:|---|
| 0 | 5 | Numeric display 1: five normalized segment slots |
| 5 | 5 | Numeric display 2: five normalized segment slots |
| 10 | 15 | Five dot-matrix rows, three bytes per 21-bit row |
| 25 | 5 | Numeric display 3: five normalized segment slots |
| 30 | 5 | Numeric display 4: five normalized segment slots |

This buffer contains normalized logical segments, not SPI-ready PCB data. Matrix bits 21 through 23 are unused and must be zero. Numeric displays 1 to 4 here, and in the wiring tables below, are indices 0 to 3 of the software interface.

## PCB Encoding

The PCB encoder converts a logical board state into a prepared frame containing five multiplexing slots of seven SPI bytes, for a total of 35 bytes. `encodePasses()` runs it eight times, once per blink phase and pass, on the elements that phase and pass show (`passElements()`: the content masked by the level planes, the blink plane and the matrix's pass mask), giving the 280-byte `PassFrames` the refresh reads.

For each slot, the seven bytes are defined in physical order from the first to the last device in the daisy chain:

1. Numeric display 1.
2. Numeric display 2.
3. Dot matrix byte 1.
4. Dot matrix byte 2.
5. Dot matrix byte 3.
6. Numeric display 3.
7. Numeric display 4.

Because the last device must be transmitted first, the SPI transfer order is the reverse of that physical listing. The exact wire order is:

1. Numeric display 4 byte.
2. Numeric display 3 byte.
3. Dot matrix byte 3, with logical matrix bits 21 through 23 set to zero because matrix columns use only bits 0 through 20. The byte's serialized bit order is still MSB first.
4. Dot matrix byte 2.
5. Dot matrix byte 1.
6. Numeric display 2.
7. Numeric display 1.

The receiver/encoder on a Display Controller uses the same named physical order, so the transmit and receive ends agree without relying on C++ struct layout. The SPI peripheral is configured MSB first, so each byte is sent bit 7 first and no bit reversal is required.

Reversing the seven byte positions cannot be achieved by the MSB-first setting: MSB-first controls bit order inside each byte, not the order of bytes in the transfer. No bit reversal or other bit-level computation is required. The encoder can write the seven-byte prepared slot directly in wire order, or transmit a physical-order array using reverse indices. Because the transfer is only seven bytes, either approach is acceptable; the chosen implementation must not reverse bits inside the bytes.

The encoder reads normalized A-G and DP segment values, then applies the wiring table for the corresponding numeric display. An SCT output value of `1` turns on the connected LED output.

### Numeric Segment Wiring

| Segment | Display 1 bit | Display 2 bit | Display 3 bit | Display 4 bit |
|---|---:|---:|---:|---:|
| A | 2 | 5 | 0 | 0 |
| B | 1 | 6 | 1 | 1 |
| C | 4 | 1 | 3 | 7 |
| D | 3 | 3 | 6 | 4 |
| E | 6 | 2 | 5 | 5 |
| F | 0 | 7 | 2 | 2 |
| G | 7 | 4 | 7 | 3 |
| DP | 5 | 0 | 4 | 6 |

### Multiplexing Mapping

- `DISPLAY_1_EN` through `DISPLAY_4_EN` select numeric digit positions 1 through 4 and dot-matrix rows 1 through 4.
- `DISPLAY_5_EN` selects the special-indicator position for numeric displays and dot-matrix row 5. Only L1, L2, and L3 are populated in the numeric-display position selected by `DISPLAY_5_EN`.
- Hardware matrix rows count from the bottom, the opposite of the logical rows: `DISPLAY_1_EN` drives the bottom row, which shows logical row 4, and `DISPLAY_5_EN` drives the top row, which shows logical row 0. `DisplayCodec::encodePcb()` reverses the order (slot `n` carries logical row `4 - n`), so the local board's row 4 progress bar appears at the bottom.
- All `DISPLAY_x_EN` outputs are active-low: drive them high to disable and low to enable.
- The numeric minus sign uses segment G.
- On each numeric display, special indicators L1 and L2 are the two dots between the second and third digits; L3 is the apostrophe before the fourth digit.
- L1 uses the numeric display's segment-A mapping, L2 uses segment-B mapping, and L3 uses segment-C mapping while `DISPLAY_5_EN` is active.
- L3 is not currently exposed through the public interface and remains off unless future API support is added.
- Dot-matrix columns 1 through 21 map directly to SCT bits 0 through 20. Bits 21 through 23 are zero.

## Refresh Operation

A complete multiplexing frame is five slots of 4 ms, a 50 Hz frame. Each slot is shown as four passes (`RefreshSequencer`), longest first, so the slot switch and its settle time come out of the longest pass rather than the shortest: with the default table the order is 39, 30, 19 and 12 % of the slot, 1560, 1200, 760 and 480 us.

The whole refresh runs from the timer's update interrupt, once per pass, with no task:

1. **Latch** the data the DMA shifted in during the pass that has just ended. At the first pass of a slot this is the slot switch: blank the drivers' outputs with OE/ (`SCT_ENABLE` high), drive the previous `DISPLAY_x_EN` high, pulse the latch, drive the new `DISPLAY_x_EN` low, wait `kSlotSettleMicros` (10 us, timed from SysTick) and re-enable the outputs. Within a slot it is just the latch pulse: the SCT drivers keep their outputs while LA/ is low (SCT2024 truth table), so the display is not disturbed by the shift. If the previous shift is somehow still running, the interrupt was later than a whole pass; the old data stays on and `lateShifts` counts it.
2. **Set the length** of the pass that starts now, by writing ARR (auto-reload preload is off, so the write applies to the running period). If the interrupt came later than the pass is long, the counter is already past the new reload and would run on to the timer's full range before the next update, 71 minutes on the 32-bit TIM2, so the counter is restarted instead and `lateInterrupts` counts it; that pass shows for its length plus the delay. This happened once, at boot, in the thick of the WiFi module start-up.
3. **Start the DMA** that shifts the next pass's seven bytes (56 us at 1 MHz) while this pass is lit. At a frame boundary a pending submission is swapped in first, so a frame never mixes two submissions.

The display is therefore dark only for the slot swap, about 12 us per slot, and the pass timing depends on interrupt latency alone: 13 us typically, up to about 260 us measured under WiFi load, against the 480 us shortest pass. The settle time lets the old slot's switch finish turning off before the outputs return: the Si2333DDS high-side P-MOSFET is switched on hard through a BC847 but turned off only by its gate pull-up resistor, and returning the outputs too early would show a faint copy of the new slot's pattern on the old one (ghosting). If ghosting is visible, raise `kSlotSettleMicros` in `PcbDisplayBoard.cpp`.

`submit()` encodes the content and attributes into the back frame set (`encodePasses()`, about 1.5 ms at `-O0`, from the caller's task and outside any lock the interrupt needs) and asks for a swap; the interrupt swaps the sets before it shifts the first pass of the next frame. A submission still waiting to be shown is overwritten by a newer one. Setters remain unsynchronized: the most recent update to a field wins, and a submission may combine fields from different clients; `Display::submit()` serializes the transfers.

The interrupt takes up to about 170 us at `-O0` (a slot switch plus the HAL DMA start), 1000 times a second: about 2-3 % of the CPU, and that long a hold-off for every other interrupt of the same NVIC priority. `status` reports `frames` (should grow by 50 a second), `late shifts`, `late interrupts` and the longest interrupt; the DisplayController copies the same counters into `g_displayStats`.

SPI3 runs at 1 MHz (prescaler 16) with `SPI3_TX` on DMA1 channel 4; the DMA interrupt is needed by the HAL to finish a transfer's bookkeeping. The SCT2024 accepts up to 25 MHz; above about 4 MHz the SCK/MOSI pins (PB3/PB5) would also need a faster GPIO speed than the current `GPIO_SPEED_FREQ_LOW`.

TIM2 configuration, from the 16 MHz HSI timer clock:

- Prescaler: `15`, a 1 MHz counter clock, so a pass length is written in microseconds.
- Auto-reload period: `3999` in CubeMX, a placeholder; the interrupt rewrites it every pass.
- Counter mode: up-counting, clock division 1, auto-reload preload disabled (it must stay off).
- TIM2 update interrupt and its NVIC entry enabled in CubeMX, priority 3 like the other peripherals.

`HAL_TIM_PeriodElapsedCallback()` in `main.c` forwards every timer to `Display_PcbTimerElapsed()` from its user-code section, which runs the refresh for TIM2. TIM1 provides the HAL time base and stays separate. TIM2 is primed and started by `PcbDisplayBoard::start()`. The DisplayController does the same with TIM6 and SPI1 on DMA1 channel 1.

Before this design the refresh was a task woken by the timer, which took the mutex `submit()` held while encoding and then spent about 210 us in the blocking SPI HAL before a slot could change; any task-level stall moved the slot boundary with it, and the lwIP `netif` task, created by the ST67 driver above the refresh task's priority, held slots for up to 5 ms during a WiFi refresh. Both were found with the pass timing, which made them visible as flicker on the dim levels, and fixed before the interrupt-driven refresh replaced the task.

## I2C Transport

I2C1 is enabled in the CubeMX configuration on PA9/SCL and PA10/SDA using 7-bit addressing. The Host Controller acts as controller/master, and each Display Controller acts as target/slave.

The bus needs external 2.2k pull-up resistors to 3V3 on SCL and SDA. They are sized for the eventual bus of one settings EEPROM plus up to ten Display Controllers, roughly 300-450 pF, where the more common 4.7k would exceed the 1 us rise time that 100 kHz standard mode allows. Without pull-ups the lines can never be released high, the peripheral latches BUSY on its first START, and no device on the bus responds.

I2C1 is shared with the settings EEPROM described in [Settings.md](Settings.md), which is driven from a different task. All traffic therefore goes through `Device::I2cBus`, which owns the handle and the mutex serializing one transfer at a time. `Display::submit()`'s own mutex serializes display refreshes against each other but does not cover other clients of the bus, so any new I2C device must be given the same `I2cBus` rather than the raw `I2C_HandleTypeDef`.

Each message contains 36 bytes: one command byte and one 35-byte plane in the logical board layout:

| Offset | Size | Content |
|---:|---:|---|
| 0 | 1 | Command |
| 1 | 35 | Logical board buffer, or one attribute plane in the same layout |

| Command | Plane | Receiver |
|---:|---|---|
| `0x01` | Content | Replaces the content, applies the staged attributes, shows the board |
| `0x02` | `BoardAttributes::blink` | Staged |
| `0x03` | `BoardAttributes::level0` | Staged |
| `0x04` | `BoardAttributes::level1` | Staged |

The 35-byte logical payload is serialized in this order: numeric display 1, numeric display 2, matrix rows 0 through 4, numeric display 3, and numeric display 4. Each numeric display occupies five bytes, one normalized segment byte for each multiplexing slot. Each matrix row occupies three bytes in little-endian order, with bit 0 in the first byte's least-significant bit. The unused bits 21 through 23 are zero. The same serialization is used when packing on the Host Controller and unpacking on the Display Controller.

The host sends a board its attributes first, `0x02`, `0x03`, `0x04`, then its content, `0x01`, about 3.6 ms per message at 100 kHz. The Display Controller stages the attribute planes and applies them together with the next content, so content and attributes always change as one; staged attributes persist until the host replaces them, so a host that sends only content keeps the last attributes, and one that never sends any (or a Display Controller firmware from before the attributes, which drops the unknown commands) shows full brightness and no blinking. The Display Controller performs its own PCB-specific encoding. Unknown commands are counted and ignored. There is no application-level response or success message; normal I2C ACK/NACK behavior still applies.

Each Display Controller has three address-programming pins, `ADDR_0` (PB10), `ADDR_1` (PB11) and `ADDR_2` (PB14), as named in `Core/Inc/main.h`. Each pin can be tied to ground, tied to VCC, or left floating, providing 27 possible ternary board IDs. The Display Controller derives its 7-bit I2C target address as `0x10 + board_id`, giving addresses `0x10` through `0x2A`.

The Host Controller does not derive these addresses from its own pins. `AstroWeather.cpp` creates one buffer-backed Display Board per remote board at the fixed addresses `0x10` through `0x14`, and `Display::submit()` sends each logical buffer to its board's address.

Address detection (`Display::detectBoardId()` in `DisplayAddress.cpp`) uses two reads for each pin:

1. Configure the pin as a digital input with an internal pull-down and read it. HIGH means VCC, state 2.
2. Otherwise switch to an internal pull-up and read again. LOW means a strong external ground, state 0; HIGH means floating, state 1.

The pins are then left in analog mode, so a strap tied to VCC draws no pull current and an open one leaves no floating digital input, and `board_id = ADDR_0 + 3 × ADDR_1 + 9 × ADDR_2`.

On receipt, `deserializePlaneI2c()` in `DisplayI2cProtocol.cpp` accepts only a 36-byte message with a known command, reports which and decodes its plane; the Display Controller's `FrameAssembler` then stages or applies it. `deserializeI2c()` is the content-only form. A rejected message changes nothing. All of this has native tests; see [Tests](#tests).

## Refresh Progress

While an astro refresh runs, the bottom row (row 4) of the Host Controller's
own matrix shows its progress in six segments, the current one blinking with
the display's own blink attribute. Remote boards are not affected; their row 4
is always blank. The behaviour, segment boundaries and success and
failure indications are described in
[AstroRefresh.md](AstroRefresh.md#progress-bar). Typical step times:

| Segment | Columns | Step | Typical time |
| --- | --- | --- | --- |
| 1 | 0-2 | Start the WiFi module | ~15 s on the first refresh after boot |
| 2 | 3-6 | Join WiFi | ~2-3 s |
| 3 | 7-9 | Get an IP address (DHCP) | < 1 s |
| 4 | 10-13 | Download (DNS and HTTP) | ~2 s |
| 5 | 14-16 | Disconnect | ~1 s |
| 6 | 17-20 | Check, parse and publish | milliseconds |

Stuck at segment 2 is a WiFi problem, at segment 4 the network or server; the
console log carries the detail.

Verified by reading the local board's row 4 over SWD during refreshes: the
segment values progressed `0x07`/`0x7F` (joining) through `0x3FF`/`0x3FFF`
(downloading) and `0x1FFFF` (disconnecting) to `0x1FFFFF` (success).

## Low Brightness

LED brightness is analog. The light-sensor divider sets `LED_BRIGHTNESS`, which runs to every board on pin 10 of `J102`/`J104`. On each board, one current-set stage per driver (`MCP6006` + `BC847`, `U503`/`Q506` and the rest) turns that voltage into the SCT `REXT` current. The firmware does not set a brightness value. It has one lever: `LOW_POWER_ENABLE` (`PB8`, pin 9 of the same connectors), which switches on `Q701`. `Q701` shorts `R705` at the bottom of the divider and lowers `LED_BRIGHTNESS` by a fixed ratio, from about 1.17 V to 0.49 V in bright light. The level still follows the ambient light.

Only the Host Controller drives the pin, through `LowBrightness::set()`/`toggle()` in `User/Src/Display/LowBrightness.cpp`. Two controls change it:

- `display low on|off` on the console sets it and saves it (settings tag `DisplayFlags`, see [Settings.md](Settings.md#tag-registry)). `AstroWeather_Init()` applies the saved state before the local board starts. `display low` reports the state in use and the saved one, and `status` shows it as `brightness normal|low`. See [Console.md](Console.md#display).
- Switch 2 toggles it, logs `Low brightness on` or `off`, and saves it the same way from `MainLoopTask`. `Settings::Store::save()` holds the store's lock, so a press cannot interleave its EEPROM page writes with a console save. The switch is debounced in hardware only. `SWITCH_2` is `SW301`: pulled up through `R306` (10k) and filtered by `R308` (10k) and `C308` (100 nF), so τ ≈ 1 ms on press and 2 ms on release, into the Schmitt-trigger input. A bounce longer than that toggles twice and saves twice.

Measured on the host board on 2026-09-24, with all LEDs lit (`8.888` on the four numeric displays, full matrix) and alternating the two states three times in the same light, at 5 V:

| State | Board current | LEDs (minus the 14 mA with all LEDs off) |
| --- | --- | --- |
| Normal | 73–79 mA | 59–65 mA |
| Low | 42 mA | 28 mA |

Low brightness cut the LED current by about 53% (to 0.45 of normal), or the whole board's current by about 45%. With all LEDs off, both states draw 14 mA. The divider alone predicts 0.32–0.42 of normal, depending on the light (1.5 kΩ instead of 4.7 kΩ at the bottom), so the current-set stages do not scale exactly with `LED_BRIGHTNESS`. The ratio will differ in other light levels.

The net is bussed to every board, so the Display Controller's CubeMX configuration leaves its own `PB8` in analog mode and never drives it ([Hardware review](../../../KiCad/Hardware_Review.md) M-4).

## Lifecycle

### Host Controller

`AstroWeather.cpp` creates:

- The local PCB-backed Display Board, refreshed from TIM2's interrupt with SPI3 DMA.
- Five buffer-backed boards at I2C addresses `0x10` through `0x14`, on the shared `Device::I2cBus`.
- The top-level `Display` containing the local board and the five remote boards.

Clients call `Display::submit()` after they have finished updating the boards. Setters are intentionally unsynchronized, so independent clients may overwrite pending fields; the last update to each field wins. `submit()` serializes the hardware transfer sequence, submits the local content and attributes to the PCB-backed board for encoding, then sends each remote board its three attribute planes and its content at its configured I2C address.

Each remote transfer is bounded by a 50 ms timeout rather than `HAL_MAX_DELAY`, so an unreachable board cannot block the calling task; a board's four messages stop at the first failure. A board's reachability is logged on transition, and an unreachable board is restated every 30 seconds while it keeps being refreshed; logging every failure could flood the console, while logging only the transition would lose the message entirely for a board missing from boot, which fails before USB CDC has enumerated. A failed transfer is not retried; the board gets the data at the next `submit()`.

Remote boards are refreshed by `Display::submit()` only, which only astro refreshes and `display` commands call. Updates that touch just the local board, namely the current-sense readout, the clock and the refresh progress bar, use `Display::submitLocal()` and send nothing over I2C, so an unreachable board is reported when a refresh actually tries to reach it, not continuously.

### Display Controller

The separate [DisplayController](../../DisplayController/README.md) project creates one PCB-backed board on SPI1 and TIM6, reads its address from the straps, and listens on I2C1 at that address. Each 36-byte message is received in interrupts and queued (four deep); its `DisplayApp` task feeds them to `FrameAssembler`, which stages the attribute planes and applies them with the content, and shows the result. Anything else is counted and dropped. It also shows boot screens (all segments, then its address), the "no data" state, and test screens on its switches, all plain: full brightness, no blinking. Built and unit tested; the attribute messages and the interrupt-driven refresh have not yet been run on a display board. See its [Architecture.md](../../DisplayController/docs/Architecture.md).

## Tests

- `../Common/tests/NumericDisplayTests.cpp`: `setFixed()`, including magnitudes below one and values that do not fit, both `setValue()` overloads, `setTime()` including the blank leading zero and the error pattern, `setTimeUnset()`, `setBlank()`, `setSegments()`, `setNoData()` and `noDataState()`.
- `../Common/tests/DisplayCodecTests.cpp`: golden vectors from the tables above: every segment of every digit of every numeric display on its documented bit, byte and slot, the indicators, the 21 matrix columns and bits 21-23, and the order of the five matrix rows in the prepared frame.
- `../Common/tests/DisplayI2cProtocolTests.cpp`: the 36-byte layout, the round trip, masking of bits 21-23, and rejection of short, long and null messages and unknown commands, leaving the destination untouched.
- `../Common/tests/DisplayAddressTests.cpp`: all 27 strap combinations through the stub GPIO, the pins left analog without pull, `boardAddress()` limits and `detectBoardAddress()` on `ADDR_0`-`ADDR_2`.
- `../Common/tests/DisplayAttributesTests.cpp`: the attribute defaults, the blink and level setters with their masks, clamping and out-of-range indices, and the plane operations (`&`, `|`, `~` keeping bits 21-23 zero).
- `../Common/tests/DisplayPassesTests.cpp`: the pass table's invariants (sums to 100, the level percentages, the matrix's 70 %), which elements each pass and blink phase show, and that `encodePasses()` is `encodePcb()` of exactly those, with spot checks against the wiring.
- `../Common/tests/RefreshSequencerTests.cpp`: the pass lengths from the table and their validation, the longest-first order and slot and frame boundaries over a frame, `peek()` not advancing, and the blink phase per frame.
- `../../DisplayController/tests/FrameAssemblerTests.cpp`: the staging rules on the receiving side.

Not covered: the refresh interrupt itself and transfer failures.

## Remaining Implementation Work

1. Run the attribute messages and the interrupt-driven refresh on a display board.
2. Decide whether a failed remote transfer should be retried before the next `submit()`.
