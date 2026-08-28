Display Board - PCB version
- Single PCB with multple led displays, driven using SCT2xxx chipt and multiplexing.
- There are number displays and dot matrix displays
- Each board has 5 multiplexed "columns" (for number displays: 4 digits + special dots, 5 dot rows)
- Refresh rate should be less than 20/5 ms (20ms for cycle of 5 columns)
- for each column cycle: send data using SPI, then latch using LATCH pin (low then high)
- each column is driven by mosfet controlled by output pin (low to enable)
- Has public interface that allows to set data to be displayed:
```
displayBoard.number[col][row] = 123.4
displayBoard.number[col][row] = 123.4
displayBoard.number[col][row] = 12:30 // this is meant to set time, exact way of passing that to be defined
displayBoard.dots[row] = xxxx // value representing 21 dots
```
- Display Board is configured using I2C address (constructor)
- Display Board uses FreeRTOS Task to periodiacally send data through SCT device and multiplexing.
- Display Board stores data to be displayed in format that can be immediatelly being sent to SCT. It means that value to be displayed needs to be transformed to ready-to-sent data. Such data is merged between all led displays from given PCB.
- Transformation
-- Number displays
--- Input value needs to be split into separate digits and dots, double-dots (for times)
--- Each digit needs to be translated to byte coding A-G, DP segments. Each number display may have different wiring for each of the segments. (how to traslate? maybe first translate to normalized A-G,DP value and then translate it to fit wireing of given led display. lookup table? some bit shifting logic?)
-- Dot matrix
--- Input value representing 21 dots to be converted into 21 bits
- Merging led displays
-- led displays are daisy-chained: two number displays (two bytes), one 5x21 dots display (three bytes, last three bits are ignored, but has to be sent), another two number displays (two bytes). It means that 7 bytes need to be sent in one multiplexing cycle 

Display Board - Buffer version
- Used by Display to store data to be sent

Desplay
- There are multiple Display Boards connected using daisy chain. First board is in Host Controller mode that contains Wi-Fi that fetches data to display and talks with other PCBs, those in Display Controller mode. Display Controller PCBs just listens for data from Host and displays its portion of data. Controllers comunicates using I2C. Each board has unique I2C address (derived from programming pins). Host Controller also contains display.
- Display is a virtual (logical) representation of data to be displayed, split by Display Board. Display object exist on Host Controller only, which uses one Display Board for displaying on its own PCB, and sending rest to other Display Controllers.
- Display has collection of Display Boards object to set current values to display. One Display Board object drives led displays from current PCB, other contain buffers to be sent to other PCBs.
- Display is built (instatiated) by creating objects for Display Boards (I2C addresses are know ap front)
- Display exposes boards through public interface so that client code can set new values to be displayed
- Display exposes method for sending buffered data to Display Controllers. It go trough Display Boards (buffer version) and sends the data to the proper I2C address.

Host Controller
- Display object
- Display Board - PCB version
- Four Display Boards - Buffer version
- Display can send data to Display Controllers

Disply Controller
- Receives data from Host Controller
- Display Board - PCB version

Outstanding implementation issues
- Hardware mapping must be documented before implementation:
	- Define the seven-byte shift-register order on the daisy chain.
	- Define the bit assigned to every A-G, DP, and double-dot segment.
	- Define the bit order and orientation of the 5x21 dot matrix.
	- Define which physical column drives each number digit, special-dot group, and dot-matrix row.
	- Define whether each SCT output and each display column is active-high or active-low.

Clarification:
Mappings:
first led display
a - 2 (segment a is on second bit) 
b - 1
c - 4
d - 3
e - 6
f - 0
g - 7
DP - 5

second led display
a - 5
b - 6
c - 1
d - 3
e - 2
f - 7
g - 4
DP - 0

led dot matrix
1 - 0
2 - 1
3 - 2
4 - 3
...
21 - 20
bits 21-23 are not used, but need to be sent to align to 3 bytes. value doesn't matter, could be zeros

third led display
a - 0
b - 1
c - 3
d - 6
e - 5
f - 2
g - 7
DP - 4

fourth led display
a - 0
b - 1
c - 7
d - 4
e - 5
f - 2
g - 3
DP - 6

all led displays
DISPLAY_1 - DISPLAY_4 drives column X
DISPLAY_5 drives special dots: L1 and L2 (two dots between second and third digit for dividing hour and minuts), L3 (apostrophe before fourth digit). L1 has the same mapping as 'a' segment, L1 same as 'b' segment, L3 same as 'c' segment.

dot matrix display
DISPLAY_1 to DISPLAY_5 drives rows
data sent to SCT drives columns

DISPLAY_X are driven low
SCT values are driven with '1's

- Confirm SCT2xxx electrical and timing requirements from the datasheet and schematic:
	- SPI clock polarity, clock phase, bit order, and maximum frequency.
	- Required latch idle state and active edge. The current text says low then high, while the existing SCT driver currently pulses high then low.
	- Whether all seven bytes must be shifted before one latch operation.
	- Output-enable timing during shifting and column changes to prevent ghosting.

Clarification:
- current setup is confirmed to be working. later, spead will be increased. it may stay low for now
- Existing SCT driver confirmed to be working correctly
- yes, all 7 bytes to be shifted before latching
- disable displays duing shifting (thorugh putting DISPLAY_x high)

- Define the refresh timing precisely. The current requirement of "less than 20/5 ms" should be replaced with a frame period, slot period, minimum refresh rate, and permitted jitter. For example: five slots, 4 ms per slot, 20 ms per frame, at least 50 Hz.

Clarification:
- one requirement - entire multiplexing cycle should be done with at least with 50 Hz refrasze rate
Each digit from number display and dot matrix row should be activated for one fifth of entire cycle period

- Define the refresh sequence for one slot, including disabling the active column, shifting seven bytes, latching, selecting the next column, and enabling it.

Clarification:
1. Disable previous slot: DISPLAY_x
2. shift 7 bytes for current slot
3. latch
4. Enable current slot: DISPLAY_y
5. Idle for 4/5ms


- Define the public C++ data model and indexing. Specify what `number[col][row]` means, and replace illustrative assignments such as `12:30` with typed operations or a precise value representation.

clarification
- ok, drop [col][row]. let's use:
numeric[i].setValue(x, p) - for real numbers (x) with precision (p) - where to place decimal-point: 0 - no decimal points / only integers, 1 - one decimal number, 2 - two decimal numbers. p is 0 by default
numeric[i].setTime(uint8_t hour, uint8_t minute) - for times
i - index from 0 to 3, for four led number displays
matrix[j].setRow(x) - x is 32 bit variable first 21 bits are driving columns
j - row index, from 0 to 5 

Numeric input and internal storage
- The public interface may accept several input types, but every accepted value is converted to one canonical fixed-point representation before display encoding.
- Canonical numeric storage:
	- `int16_t mantissa`: the displayed number with its decimal point removed.
	- `uint8_t flags`: display mode and segment modifiers.
	- The displayed value for a numeric value is `mantissa / 10^precision`, where `precision` is decoded from `flags`.
- Recommended flag layout:
	- Bits 0-1: display mode: `0 = Blank`, `1 = Value`, `2 = Time`, `3 = reserved`.
	- Bits 2-3: decimal position: `0 = none`, `1 = after the first digit`, `2 = after the second digit`, `3 = after the third digit`.
	- Bit 4: double dots enabled, used by time display.
	- Bits 5-7: reserved and written as zero.
- A blank display is represented by `mantissa = 0` and mode `Blank`; the formatter must turn off all segments regardless of the mantissa value.
- A time is represented by `mantissa = hour * 100 + minute`, mode `Time`, and the double-dot flag. Time formatting always renders four positions as `HHMM`, so values such as `03:07` retain both leading zeroes.
- The internal representation should be separated into two layers:
	- Logical state: four numeric-display contents and six dot-matrix row values.
	- Prepared frame: five multiplexed columns, with seven bytes per column. Each byte contains the already mapped SCT output bits for the four numeric displays, special dots, and dot matrix.
- Input methods update the `(mantissa, flags)` logical state, validate it, encode the affected display content, merge all content into a new prepared frame, and publish that frame atomically. The refresh task only reads the published prepared frame; it does not perform float conversion, decimal formatting, or segment mapping in the refresh loop.
- The prepared frame layout follows the physical chain for every multiplexed column: first numeric display, second numeric display, dot matrix (three bytes), third numeric display, and fourth numeric display. It therefore contains `5 * 7 = 35` bytes. The three unused dot-matrix bits must be written as zero.
- The segment mappings from the hardware clarification are encoder configuration tables, not part of the public numeric API. The encoder first creates normalized A-G, DP, and special-dot segment values, then applies the per-display bit mappings before merging the seven output bytes.
- Recommended methods:
	- `setFixed(int16_t mantissa, uint8_t precision = 0)` for exact values already expressed in fixed-point form; this method converts `precision` into the decimal-position flags.
	- `setValue(int16_t value)` or another suitable signed integer overload for whole numbers.
	- `setValue(float value, uint8_t precision = 0)` for convenient real-number input.
	- `setTime(uint8_t hour, uint8_t minute)` for time values; this converts the inputs to `mantissa = hour * 100 + minute` and time flags.
	- `setBlank()` to set mode `Blank` and clear all display modifiers.
- The float overload must convert deterministically by rounding `value * 10^precision` to the nearest integer mantissa, then validating the resulting mantissa and display fit. It must reject NaN, infinity, unsupported precision, and values that do not fit the four-digit display. It must also define the behavior of exact half-way values and negative zero.
- The float overload is an input convenience only. The display task and I2C buffer must store and transmit the canonical `(mantissa, flags)` pair or the resulting encoded display bytes, never a float.
- Example conversions:
	- `setValue(1234, 0)` stores `mantissa = 1234` and Value/no-decimal flags, then displays `1234`.
	- `setValue(123.4f, 1)` stores `mantissa = 1234` and Value/decimal-after-first-digit flags, then displays `123.4`.
	- `setValue(12.34f, 2)` stores `mantissa = 1234` and Value/decimal-after-second-digit flags, then displays `12.34`.
	- `setFixed(-999, 1)` stores `mantissa = -999` and Value/decimal-after-first-digit flags, then displays `-99.9`.
	- `setTime(3, 7)` stores `mantissa = 307` and Time/double-dots flags, then displays `03:07`.
	- `setBlank()` stores `mantissa = 0` and Blank flags.
- Integer and fixed-point overloads should be preferred in firmware paths where exact decimal behavior matters. Float input is appropriate for configuration or application values, provided the caller supplies the intended precision.

- Define formatting behavior for numeric values:
	- Supported range and precision.
	- Decimal-point placement.
	- Leading zero and blank handling.
	- Negative values and overflow.
	- Invalid time values and unsupported values.

Clarification
- for numerical value range is limited by 4 digits: 4 digit positive numbers, 3 digit negative numbers
- decimail-point depends on precision parameter (p)
- leading zeros are blank
- negative values proceded with minus symbol (g-segment)
- hours and minuts: from 00 to 99

- Define the prepared-buffer layout and ownership. State whether the buffer contains one column, one complete five-column frame, or all 35 bytes, and how local and remote boards share the encoder logic.

Clarification:
- buffer contains internal storage for every led display: numerical display - mantissa and flags (3 bytes), dot matrix - 5*3 bytes
- it doesn't map into ready to send to SPI data
- Display Board (PCB version) on Display Controller will take those values and then map according to wireing

- Define update synchronization. Updates must not produce mixed frames while the refresh task reads the buffer. Specify whether double buffering is used and when a new frame becomes visible.

clarification
- display is being refreshed 20 times per second. if one frame is currupted then the next one would be fixed. if there is no strong need for better synchronisation, it can be ignored for now

- Define the I2C transport protocol:
	- Address derivation, valid address range, and reserved addresses.
	- Command or message type, protocol version, payload length, byte order, and checksum if needed.
	- Whether I2C carries logical values or prepared display bytes.
	- Atomic update behavior on the Display Controller.
	- Timeout, retry, malformed-packet, offline-board, and partial-transfer behavior.

Clarification
- Each board has three pins that could be used for programming I2C address. It is possible to short them to ground, Vcc or left floating. It allows to establish 3 state logic to every pin, meaning 3 pow 3 different addresses (27). This value could be added to some predefined base address establishing specific I2C address.
- I2C message: 1 byte to establish command and data format and then buffered data for given Display Board: first number display (mantissa, flags), second number display, dot matrix (5 times 3 bytes), third number display, fourth number display.
- currently, there will be only one command / data format: 0x01 indicating 'set display board values as described above'
- I2C carries logical values
- no need for atomic update on Display Controller
- very basic error handling if any. no return data for now (Display Controller don't respond with success / ack message)


- I2C is not currently enabled in the STM32 HAL configuration. The `.ioc` must define the I2C peripheral, pins, timing, interrupts or DMA if required, and controller/slave roles before code implementation.

clarification
- it is enabled now. check it

- Define task lifecycle and ownership:
	- Which object creates and starts the FreeRTOS refresh task.
	- Which object owns SPI, latch, enable, and column GPIO access.
	- How Host Controller and Display Controller variants instantiate and initialize their board objects.

clarification
- AppVariant.cpp will create Display Board (PCB variant) object that inits and start that task
- Display Board (PCB) own SPI and other pins


- Add acceptance tests with exact expected output bytes. At minimum, cover a digit on every display position, decimal and double-dot segments, representative dot-matrix patterns, blanking, frame updates, and I2C transfer errors.

Both variants of Dispaly Boards have the same interface. Client code uses them both withing knowing which version it is