# Display Functionality

## Overview

The system contains one Host Controller board and up to four Display Controller boards. Every board has the same physical display hardware:

- Four multiplexed four-digit, seven-segment numeric displays.
- One 5x21 dot-matrix display.
- Three special indicator dots.
- SCT2xxx LED drivers connected as one SPI daisy chain.
- Five active-low multiplexing outputs, `DISPLAY_1_EN` through `DISPLAY_5_EN`.

The Host Controller fetches application data, displays its local portion, and sends the remaining board values to Display Controllers over I2C. A Display Controller receives logical display values over I2C and renders them on its local PCB.

## Software Architecture

### Display

`Display` exists only in the Host Controller firmware. It is the logical representation of the complete multi-board display and owns a collection of board interfaces:

- One local PCB-backed Display Board.
- Four remote buffer-backed Display Boards, one for each Display Controller.

Client code accesses all boards through the same Display Board interface without needing to know whether a board is local or remote. `Display::submit()` submits the local board's logical buffer for PCB encoding and periodic SPI refresh, then sends each remote board's logical buffer to its configured I2C address.

### PCB-backed Display Board

The PCB-backed implementation:

- Owns the SCT2xxx SPI interface, latch and enable signals, and the five multiplexing GPIOs.
- Stores logical display values.
- Converts logical values into PCB-specific segment and matrix bit mappings.
- Maintains the prepared SPI refresh data.
- Runs the local multiplexing mechanism.

`AppVariant.cpp` creates this object and starts its refresh mechanism in both firmware variants. The PCB-backed Display Board invokes `HAL_TIM_Base_Start_IT(&htim2)` when its initialization or start method is called.

### Buffer-backed Display Board

The buffer-backed implementation exists only on the Host Controller. It stores logical board values in the I2C payload format and does not apply PCB wiring mappings. Its data is sent to a Display Controller, where the PCB-backed implementation performs the mapping.

### Code Ownership

All application-side display code is located under `/User`:

- `/User/Device` contains low-level device implementations, such as the SCT2xxx driver.
- `/User/Display` contains all display-related code, including Display, Display Board variants, numeric and matrix content types, encoding, refresh, and I2C display transport logic.

## Public Interface

Each board exposes four numeric displays indexed from `0` through `3` and five dot-matrix rows indexed from `0` through `4`.

Each of the four numeric displays has its own three special indicators: L1 and L2 form the double dots used for time, and L3 is the apostrophe before the last digit. `DISPLAY_1_EN` through `DISPLAY_4_EN` select the four numeric digit positions on every numeric display. `DISPLAY_5_EN` selects the special-indicator position on every numeric display; only the three special-indicator segments are used in this position.

The intended interface is:

```cpp
numeric[i].setFixed(int16_t mantissa, uint8_t precision = 0);
numeric[i].setValue(int16_t value);
numeric[i].setValue(float value, uint8_t precision = 0);
numeric[i].setTime(uint8_t hour, uint8_t minute);
numeric[i].setBlank();

matrix[row].setRow(uint32_t columns);

display.submit();
```

Only the lowest 21 bits passed to `setRow()` are used. Bit 0 drives matrix column 1 and bit 20 drives matrix column 21.

Additional integer overloads may be provided. All setters convert their input to the canonical logical representation before it is stored.

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

The sign consumes the leftmost display position and leading zeroes are blank for normal numeric values. The resulting segments are stored directly in the normalized slot bytes.

Examples:

| Method | Stored mantissa | Precision | Display |
|---|---:|---:|---|
| `setFixed(1234, 0)` | 1234 | 0 | `1234` |
| `setFixed(1234, 1)` | 1234 | 1 | `123.4` |
| `setFixed(1234, 2)` | 1234 | 2 | `12.34` |
| `setFixed(1234, 3)` | 1234 | 3 | `1.234` |
| `setFixed(-999, 0)` | -999 | 0 | `-999` |
| `setFixed(-999, 1)` | -999 | 1 | `-99.9` |

The four display positions allow four positive digits or a minus sign and three digits. Values that do not fit after applying the sign and decimal precision are invalid.

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

Time mode always renders four digits as `HHMM`, including leading zeroes. For example, `setTime(3, 7)` stores mantissa `307` and displays `03:07`.

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

This buffer contains normalized logical segments, not SPI-ready PCB data. Matrix bits 21 through 23 are unused and must be zero.

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

A complete multiplexing frame consists of five slots. The complete frame rate must be at least 50 Hz, giving a maximum nominal slot period of 4 ms. SPI transfer and latch time are part of that slot period; the active dwell time is the remaining portion.

Each slot is processed in this order:

1. Drive the previously active `DISPLAY_x_EN` output high to disable it.
2. Shift all seven bytes for the next slot without latching between bytes.
3. Pulse the SCT latch using the sequence already confirmed by the existing SCT driver.
4. Drive the next `DISPLAY_x_EN` output low to enable it.
5. Keep the slot active until the next 4 ms deadline.

The current SPI mode, latch sequence, and low SPI speed have been confirmed on hardware. SPI speed may be increased later after hardware verification.

The preferred scheduling design is TIM2 providing the 250 Hz slot cadence and a high-priority refresh task performing the short seven-byte SPI transaction. The timer interrupt should only signal the task and must not call blocking SPI functions. SPI DMA may replace the blocking task-level transfer later if measured jitter or CPU use requires it.

TIM2 configuration for the initial 250 Hz slot trigger, assuming the current 16 MHz internal timer clock:

- Prescaler: `15999`, giving a 1 kHz counter clock.
- Auto-reload period: `3`, giving an update event every 4 counter ticks, or 250 Hz.
- Counter mode: up-counting.
- Clock division: divide by 1.
- Auto-reload preload: disabled initially.
- Enable the TIM2 update interrupt and its NVIC entry in CubeMX.
- Start TIM2 with interrupt generation after the display refresh mechanism has been initialized.

The update ISR should clear or dispatch the TIM2 update event through the HAL callback and signal the refresh task. Do not use the TIM2 HAL time base for the RTOS tick; TIM1 currently provides the HAL time base.

Current project status: the `.ioc` and generated `main.c` already contain TIM2 with internal clock, prescaler `15999`, and period `3`. The TIM2 update interrupt/NVIC entry, `TIM2_IRQHandler`, display-task signal path, and `HAL_TIM_Base_Start_IT(&htim2)` call are not yet present and must be added before the display refresh can use TIM2.

Logical-to-segment conversion is performed when display state changes, not in the periodic refresh loop. The refresh mechanism reads only prepared slot bytes.

The initial implementation may update prepared data without double buffering. A concurrent update may produce one mixed frame, which is accepted for the first version because the following frame corrects it. Double buffering can be added if testing shows visible artifacts.

## I2C Transport

I2C1 is enabled in the CubeMX configuration on PA9/SCL and PA10/SDA using 7-bit addressing. The Host Controller acts as controller/master, and each Display Controller acts as target/slave.

Each message contains 36 bytes. The payload has one explicit byte order used by both I2C sender and receiver:

| Offset | Size | Content |
|---:|---:|---|
| 0 | 1 | Command |
| 1 | 35 | Logical board buffer |

The 35-byte logical payload is serialized in this order: numeric display 1, numeric display 2, matrix rows 0 through 4, numeric display 3, and numeric display 4. Each numeric display occupies five bytes, one normalized segment byte for each multiplexing slot. Each matrix row occupies three bytes in little-endian order, with bit 0 in the first byte's least-significant bit. The unused bits 21 through 23 are zero. The same serialization is used when packing on the Host Controller and unpacking on the Display Controller.

Command `0x01` means "set display board values" using the logical buffer format defined above. The Display Controller checks this first command/format byte and processes the message only when it is a known value. `0x01` is currently the only known command. Unknown commands are ignored. For command `0x01`, the Display Controller replaces its local logical values and performs its own PCB-specific encoding. There is no application-level response or success message in the initial protocol; normal I2C ACK/NACK behavior still applies.

Each Display Controller has three address-programming pins, named `ADDR_1` through `ADDR_3`. Each pin can be tied to ground, tied to VCC, or left floating, providing 27 possible ternary board IDs. The Display Controller derives its 7-bit I2C slave address from these pins using `0x10 + board_id`, giving addresses `0x10` through `0x2A`.

The Host Controller does not derive these addresses from its own pins. It instantiates one buffer-backed Display Board for each Display Controller and passes that controller's I2C address to the buffer-backed board constructor. The Host-side board objects therefore retain their configured addresses and use them when `Display::submit()` sends the logical buffers.

Address detection uses two reads for each pin:

1. Configure the pin as a digital input with an internal pull-down and read it. HIGH means VCC. LOW means either ground or floating.
2. For pins that read LOW, switch to an internal pull-up and read again. LOW means a strong external ground connection; HIGH means floating.

The three detected states are mapped deterministically to a board ID from `0` through `26`. The exact state-to-bit ordering must be shared by Host and Display Controller firmware.

The Display Controller checks the first received byte before processing the payload. It reacts only to known commands; currently command `0x01` is the only valid command. Unknown commands and messages with an invalid length are ignored, and the previous logical display state is retained. Initial transport error handling may be limited to detecting HAL/I2C transfer failure and retaining the previous display state.

## Variant Lifecycle

### Host Controller

`AppVariant.cpp` creates and starts:

- The local PCB-backed Display Board and its refresh mechanism.
- The top-level `Display` containing the local board and four remote buffer-backed boards.
- Client code calls `Display::submit()` after it has finished updating all local and remote board objects. `submit()` submits the local logical buffer to the PCB-backed board for encoding and periodic SPI refresh, then sends each remote logical buffer to its configured I2C address.

### Display Controller

`AppVariant.cpp` creates and starts:

- One PCB-backed Display Board and its refresh mechanism.
- I2C target reception for command `0x01`.

## Remaining Implementation Work

1. Complete Display Controller I2C target configuration in code. The Host Controller `.ioc` configures I2C1 for transmissions; the Display Controller must reconfigure I2C1 as a target/slave using its runtime address and install the receive/listen callbacks.
2. Add the TIM2 update interrupt/NVIC entry, `TIM2_IRQHandler`, and refresh-task signal path. TIM2 is already configured for 250 Hz, and the PCB-backed Display Board must call `HAL_TIM_Base_Start_IT(&htim2)` from its initialization or start method. TIM1 currently provides the HAL time base and must remain separate.
3. Define retry and offline-board behavior for `Display::submit()` when an I2C transfer fails. The call itself and its ordering across all boards are defined above.
4. Implement and test complete-message reception so an incomplete 28-byte I2C message never partially modifies visible logical state, even though full refresh-frame double buffering is deferred. Command filtering for unknown format bytes is defined above.
5. Add acceptance tests for numeric formatting, every per-display segment mapping, decimal points, negative values, Error mode, time leading zeroes and double dots, blanking, all matrix rows and boundary bits, seven-byte SPI order, I2C serialization, command filtering, malformed messages, and transfer failures.