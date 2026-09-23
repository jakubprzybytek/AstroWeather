# Display Functionality

## Overview

The system contains one Host Controller board and up to five Display Controller boards. Every board has the same physical display hardware:

- Four multiplexed four-digit, seven-segment numeric displays.
- One 5x21 dot-matrix display.
- Three special indicator dots.
- SCT2xxx LED drivers connected as one SPI daisy chain.
- Five active-low multiplexing outputs, `DISPLAY_1_EN` through `DISPLAY_5_EN`.

The Host Controller fetches application data, displays its local portion, and sends the remaining board values to Display Controllers over I2C. A Display Controller is meant to receive logical display values over I2C and render them on its local PCB; that side is not implemented yet. See [Display Controller](#display-controller).

## Software Architecture

### Display

`Display` exists only in the Host Controller firmware. It is the logical representation of the complete multi-board display and owns a collection of board interfaces:

- One local PCB-backed Display Board.
- Five remote buffer-backed Display Boards, one for each Display Controller.

Client code accesses all boards through the same Display Board interface without needing to know whether a board is local or remote. `Display::submit()` submits the local board's logical buffer for PCB encoding and periodic SPI refresh, then sends each remote board's logical buffer to its configured I2C address. It serializes SPI/I2C transfer sequences; setters remain unsynchronized, so concurrent clients intentionally use last-writer-wins pending state.

### PCB-backed Display Board

The PCB-backed implementation:

- Owns the SCT2xxx SPI interface, latch and enable signals, and the five multiplexing GPIOs.
- Stores logical display values.
- Converts logical values into PCB-specific segment and matrix bit mappings.
- Maintains the prepared SPI refresh data.
- Runs the local multiplexing mechanism.

The HostController `AppVariant.cpp` creates this object and starts it. `start()` starts the `DisplayRefresh` task (`Task<1024>`, `osPriorityRealtime`) and then calls `HAL_TIM_Base_Start_IT(&htim2)`. The DisplayController variant does not create one yet.

### Buffer-backed Display Board

The buffer-backed implementation exists only on the Host Controller. It stores logical board values in the I2C payload format and does not apply PCB wiring mappings. Its data is sent to a Display Controller, where the PCB-backed implementation performs the mapping.

### Code Ownership

All application-side display code is located under `/User`:

- `/User/Device` contains low-level device implementations, such as the SCT2xxx driver.
- `/User/Display` contains all display-related code, including Display, Display Board variants, numeric and matrix content types, encoding, refresh, and I2C display transport logic.

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

display.submit();       // local board, then every remote board over I2C
display.submitLocal();  // local board only, no I2C traffic
```

`submitLocal()` takes the same lock as `submit()`. It is for frequent changes that touch only the local board, such as the current-sense readout, the clock and the refresh progress bar.

Only the lowest 21 bits passed to `setRow()` are used. Bit 0 drives matrix column 1 and bit 20 drives matrix column 21.

Additional integer overloads may be provided. All setters convert their input to the canonical logical representation before it is stored.

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

If a setter receives an invalid value, it stores the error pattern: segment D enabled in each of the four digit slots and the indicator slot blank.

## Logical Board Buffer

A board's transport-level logical buffer contains 35 bytes:

| Offset | Size | Content |
|---:|---:|---|
| 0 | 5 | Numeric display 1: five normalized segment slots |
| 5 | 5 | Numeric display 2: five normalized segment slots |
| 10 | 15 | Five dot-matrix rows, three bytes per 21-bit row |
| 25 | 5 | Numeric display 3: five normalized segment slots |
| 30 | 5 | Numeric display 4: five normalized segment slots |

This buffer contains normalized logical segments, not SPI-ready PCB data. Matrix bits 21 through 23 are unused and must be zero. Numeric displays 1 to 4 here, and in the wiring tables below, are indices 0 to 3 of the software interface.

## PCB Encoding

The PCB encoder converts the logical board state into a prepared frame containing five multiplexing slots of seven SPI bytes, for a total of 35 bytes.

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
- All `DISPLAY_x_EN` outputs are active-low: drive them high to disable and low to enable.
- The numeric minus sign uses segment G.
- On each numeric display, special indicators L1 and L2 are the two dots between the second and third digits; L3 is the apostrophe before the fourth digit.
- L1 uses the numeric display's segment-A mapping, L2 uses segment-B mapping, and L3 uses segment-C mapping while `DISPLAY_5_EN` is active.
- L3 is not currently exposed through the public interface and remains off unless future API support is added.
- Dot-matrix columns 1 through 21 map directly to SCT bits 0 through 20. Bits 21 through 23 are zero.

## Refresh Operation

A complete multiplexing frame consists of five slots. The complete frame rate must be at least 50 Hz, giving a maximum nominal slot period of 4 ms.

Each slot is processed in this order:

1. Shift all seven bytes for the next slot while the current slot stays lit. The SCT drivers keep their outputs while LA/ is low (SCT2024 truth table), so the display is not disturbed.
2. Blank all driver outputs with OE/ (`SCT_ENABLE` high).
3. Drive the previously active `DISPLAY_x_EN` output high to disable it.
4. Pulse the SCT latch, moving the shifted data to the outputs.
5. Drive the next `DISPLAY_x_EN` output low to enable it.
6. Wait `kSlotSettleMicros` (10 us), then re-enable the outputs with OE/.
7. Keep the slot active until the next 4 ms deadline.

The display is therefore dark only for the swap, steps 2-6, about 12 us per slot, rather than for the whole transfer. Brightness no longer depends on SPI speed. The settle time lets the old slot's switch finish turning off before the outputs return: the Si2333DDS high-side P-MOSFET is switched on hard through a BC847 but turned off only by its gate pull-up resistor, and returning the outputs too early would show a faint copy of the new slot's pattern on the old one (ghosting). If ghosting is visible, raise `kSlotSettleMicros` in `PcbDisplayBoard.cpp`. The delay is timed from SysTick, so it does not depend on compiler optimisation.

SPI3 runs at 1 MHz (prescaler 16), so a slot's 56 bits take about 56 us. The SCT2024 accepts up to 25 MHz; above about 4 MHz the SCK/MOSI pins (PB3/PB5) would also need a faster GPIO speed than the current `GPIO_SPEED_FREQ_LOW`. If a transfer fails, the current slot stays lit and the next tick tries again.

TIM2 provides the 250 Hz slot cadence and the `DisplayRefresh` task (`osPriorityRealtime`) performs the short seven-byte SPI transaction. The timer interrupt only signals the task and never calls blocking SPI functions. SPI DMA could replace the blocking task-level transfer if measured jitter or CPU use ever required it.

TIM2 configuration, from the 16 MHz HSI timer clock:

- Prescaler: `15999`, giving a 1 kHz counter clock.
- Auto-reload period: `3`, giving an update event every 4 counter ticks, or 250 Hz.
- Counter mode: up-counting, clock division 1, auto-reload preload disabled.
- TIM2 update interrupt and its NVIC entry enabled in CubeMX.

`HAL_TIM_PeriodElapsedCallback()` in `main.c` forwards every timer to `Display_PcbTimerElapsed()` from its user-code section, which signals the refresh task for TIM2. TIM1 provides the HAL time base and stays separate.

TIM2 is started by `PcbDisplayBoard::start()`, after its task. The ST67 WiFi driver's own tasks are configured just below it, so WiFi activity cannot hold up the multiplexing; see [CubeMXCompliance.md](CubeMXCompliance.md#st67-driver-task-settings).

Logical-to-segment conversion is performed when display state changes, not in the periodic refresh loop. The refresh mechanism reads only prepared slot bytes.

The initial implementation may update prepared data without double buffering. A concurrent update may produce one mixed frame, which is accepted for the first version because the following frame corrects it. Likewise, clients may update pending logical state concurrently without setter synchronization: the most recent update to a field wins, and a submission may combine fields from different clients. `Display::submit()` serializes the resulting SPI/I2C transfer sequence. Double buffering or transaction-level state locking can be added later only if a product requirement needs a coherent all-or-nothing frame.

## I2C Transport

I2C1 is enabled in the CubeMX configuration on PA9/SCL and PA10/SDA using 7-bit addressing. The Host Controller acts as controller/master, and each Display Controller acts as target/slave.

The bus needs external 2.2k pull-up resistors to 3V3 on SCL and SDA. They are sized for the eventual bus of one settings EEPROM plus up to ten Display Controllers, roughly 300-450 pF, where the more common 4.7k would exceed the 1 us rise time that 100 kHz standard mode allows. Without pull-ups the lines can never be released high, the peripheral latches BUSY on its first START, and no device on the bus responds.

I2C1 is shared with the settings EEPROM described in [Settings.md](Settings.md), which is driven from a different task. All traffic therefore goes through `Device::I2cBus`, which owns the handle and the mutex serializing one transfer at a time. `Display::submit()`'s own mutex serializes display refreshes against each other but does not cover other clients of the bus, so any new I2C device must be given the same `I2cBus` rather than the raw `I2C_HandleTypeDef`.

Each message contains 36 bytes. The payload has one explicit byte order used by both I2C sender and receiver:

| Offset | Size | Content |
|---:|---:|---|
| 0 | 1 | Command |
| 1 | 35 | Logical board buffer |

The 35-byte logical payload is serialized in this order: numeric display 1, numeric display 2, matrix rows 0 through 4, numeric display 3, and numeric display 4. Each numeric display occupies five bytes, one normalized segment byte for each multiplexing slot. Each matrix row occupies three bytes in little-endian order, with bit 0 in the first byte's least-significant bit. The unused bits 21 through 23 are zero. The same serialization is used when packing on the Host Controller and unpacking on the Display Controller.

Command `0x01` means "set display board values" using the logical buffer format defined above. The Display Controller checks this first command/format byte and processes the message only when it is a known value. `0x01` is currently the only known command. Unknown commands are ignored. For command `0x01`, the Display Controller replaces its local logical values and performs its own PCB-specific encoding. There is no application-level response or success message in the initial protocol; normal I2C ACK/NACK behavior still applies.

Each Display Controller has three address-programming pins, `ADDR_0` (PB10), `ADDR_1` (PB11) and `ADDR_2` (PB14), as named in `Core/Inc/main.h`. Each pin can be tied to ground, tied to VCC, or left floating, providing 27 possible ternary board IDs. The Display Controller derives its 7-bit I2C target address as `0x10 + board_id`, giving addresses `0x10` through `0x2A`.

The Host Controller does not derive these addresses from its own pins. `AppVariant.cpp` creates one buffer-backed Display Board per remote board at the fixed addresses `0x10` through `0x14`, and `Display::submit()` sends each logical buffer to its board's address.

Address detection (`Display::detectBoardId()` in `DisplayAddress.cpp`) uses two reads for each pin:

1. Configure the pin as a digital input with an internal pull-down and read it. HIGH means VCC, state 2.
2. Otherwise switch to an internal pull-up and read again. LOW means a strong external ground, state 0; HIGH means floating, state 1.

The pins are then returned to inputs without pull, and `board_id = ADDR_0 + 3 × ADDR_1 + 9 × ADDR_2`.

On receipt, `deserializeI2c()` in `DisplayI2cProtocol.cpp` accepts only a 36-byte message whose first byte is `0x01`, and decodes it into a temporary state before replacing the destination, so a rejected message leaves the previous state untouched.

`DisplayAddress.cpp` and `deserializeI2c()` are written for the Display Controller but are not called anywhere yet; only `serializeI2c()` is used, by `BufferedDisplayBoard`. All three have native tests; see [Tests](#tests).

## Refresh Progress

While an astro refresh runs, the bottom row (row 4) of the Host Controller's
own matrix shows its progress in six segments. Remote boards are not affected;
their row 4 is always blank. The behaviour, segment boundaries and success and
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

## Variant Lifecycle

### Host Controller

`AppVariant.cpp` creates:

- The local PCB-backed Display Board, started with its `DisplayRefresh` task and TIM2.
- Five buffer-backed boards at I2C addresses `0x10` through `0x14`, on the shared `Device::I2cBus`.
- The top-level `Display` containing the local board and the five remote boards.

Clients call `Display::submit()` after they have finished updating the boards. Setters are intentionally unsynchronized, so independent clients may overwrite pending fields; the last update to each field wins. `submit()` serializes the hardware transfer sequence, submits the local logical buffer to the PCB-backed board for encoding and periodic SPI refresh, then sends each remote logical buffer to its configured I2C address.

Each remote transfer is bounded by a 50 ms timeout rather than `HAL_MAX_DELAY`, so an unreachable board cannot block the calling task. A board's reachability is logged on transition, and an unreachable board is restated every 30 seconds while it keeps being refreshed; logging every failure could flood the console, while logging only the transition would lose the message entirely for a board missing from boot, which fails before USB CDC has enumerated. A failed transfer is not retried; the board gets the data at the next `submit()`.

Remote boards are refreshed by `Display::submit()` only, which only astro refreshes and `display` commands call. Updates that touch just the local board, namely the current-sense readout, the clock and the refresh progress bar, use `Display::submitLocal()` and send nothing over I2C, so an unreachable board is reported when a refresh actually tries to reach it, not continuously.

### Display Controller

Not implemented. The DisplayController variant's `AppVariant.cpp` only starts `ConsoleService`, with no display. It creates no PCB-backed board, so its own LEDs are not driven, and it does not configure I2C1 as a target, so it cannot receive command `0x01`. The pieces it would need exist but are unused: `DisplayAddress.cpp` for the board address and `deserializeI2c()` for the message.

## Tests

- `tests/NumericDisplayTests.cpp`: `setFixed()`, including magnitudes below one and values that do not fit, both `setValue()` overloads, `setTime()` including the blank leading zero and the error pattern, `setTimeUnset()`, `setBlank()` and `setSegments()`.
- `tests/DisplayCodecTests.cpp`: golden vectors from the tables above: every segment of every digit of every numeric display on its documented bit, byte and slot, the indicators, the 21 matrix columns and bits 21-23, and the order of the five matrix rows in the prepared frame.
- `tests/DisplayI2cProtocolTests.cpp`: the 36-byte layout, the round trip, masking of bits 21-23, and rejection of short, long and null messages and unknown commands, leaving the destination untouched.
- `tests/DisplayAddressTests.cpp`: all 27 strap combinations through the stub GPIO, the pins left without pull, `boardAddress()` limits and `detectBoardAddress()` on `ADDR_0`-`ADDR_2`.

Not covered: the refresh timing and transfer failures.

## Remaining Implementation Work

1. Display Controller: create the PCB-backed board, detect the address with `detectBoardAddress()`, configure I2C1 as a target at that address with receive callbacks, and apply complete 36-byte messages through `deserializeI2c()`. The Host Controller `.ioc` configures I2C1 as a controller; the Display Controller must reconfigure it at run time.
2. Decide whether a failed remote transfer should be retried before the next `submit()`.
