# Display on the Host

How the HostController drives its own LED board and the remote display boards.
The display model shared by every board (content, blink and levels, encoding,
the interrupt-driven refresh, boot screens) is in
[firmware/Docs/Display.md](../../Docs/Display.md); the I2C link to the display
boards is in [firmware/Docs/I2C.md](../../Docs/I2C.md).

## The `Display` Aggregate

`Display::Display` (`User/Src/Display/Display.cpp`) exists only on the host. It
holds:

- the local board, a `PcbDisplayBoard` on SPI3 and TIM2;
- one `BufferedDisplayBoard` per chain position, `0x10` to `0x15`, on the
  shared `Device::I2cBus` ([I2C.md](../../Docs/I2C.md#host-side)).

Boards are addressed by chain position (`User/Inc/Display/BoardChain.hpp`):
position *n* is the board at `0x10 + n` and shows forecast block *n*.
`setLocalAddress()`, called once at boot with the host's own strap address,
makes the local board stand in for that position; its remote board is never
used. A host strapped outside the chain shows no block.

| Call | Does |
| --- | --- |
| `local()`, `board(position)`, `remoteBoard(position)` | The boards, to write content and attributes; `board()` gives the local board at the host's own position |
| `submit()` | Shows the local board, then sends every remote board its state over I2C |
| `submitLocal()` | Shows the local board only, with no I2C traffic |
| `submitRemote(position)` | Sends one remote board; returns whether it took it |
| `runBootScreens()` | The boot screens on the local board, about 3 s ([Display.md](../../Docs/Display.md#boot-screens)) |

All submits take one mutex, so transfer sequences never interleave. Setters
are unsynchronized, last-writer-wins. Until `runBootScreens()` has finished,
submits leave the local board out; clients keep writing its state, which shows
when the boot screens end.

## Allocation on the Local Board

The astro refresh writes all four numeric displays and matrix rows 0-4 of every
board ([AstroRefresh.md](AstroRefresh.md#display-mapping)). On the local board,
other clients share some of them, through `submitLocal()`:

| Local element | Other client | Default |
| --- | --- | --- |
| Numeric 2 | Current sense, in mA, ten times a second ([CurrentSense.md](CurrentSense.md)) | on (`adc display`) |
| Numeric 3 | Clock, `HH:MM`, redrawn each minute ([RTC.md](RTC.md)) | on (`time display`) |
| Matrix row 4 | Astro refresh progress bar while a refresh runs or its failure shows; otherwise the aurora row | always |

With the defaults, the local board's night-0 maximum and minimum temperatures
are therefore overwritten: numeric 2 within 100 ms, numeric 3 at the next
minute. There is no field ownership; the last writer wins.

The console's `display` commands write any board for testing: the local one by
default, `display 0x12 ...` one remote board and `display all ...` every board;
see [Console.md](Console.md#display). A remote board keeps such a change until
the next astro refresh.

## Remote Boards

Each `BufferedDisplayBoard` keeps its board's content and attributes and sends
them as three attribute messages and the content, with a 50 ms timeout per
message, up to three attempts for a board that still answers, and warnings in
the log and error log for a board that does not
([I2C.md](../../Docs/I2C.md#buffereddisplayboard)). Remote boards are sent to
only by `submit()` (astro refresh) and `submitRemote()` (console), never by the
clock, the current readout or the progress bar.

## Refresh Progress

While an astro refresh runs, row 4 of the local matrix shows its progress in six
segments, the current one blinking; remote boards keep their aurora row. A
successful refresh replaces the bar with the aurora row at once. Behaviour,
segment boundaries and the failure indication are in
[AstroRefresh.md](AstroRefresh.md#progress-bar).

| Segment | Columns | Step | Typical time |
| --- | --- | --- | --- |
| 1 | 0-2 | Start the WiFi module | ~15 s on the first refresh after boot |
| 2 | 3-6 | Join WiFi | ~2-3 s |
| 3 | 7-9 | Get an IP address (DHCP) | < 1 s |
| 4 | 10-13 | Download (DNS and HTTPS) | ~2 s |
| 5 | 14-16 | Disconnect | ~1 s |
| 6 | 17-20 | Check, parse and publish | milliseconds |

Stuck at segment 2 is a WiFi problem, at segment 4 the network or the server;
the console log has the detail.

## Low Brightness

The analog brightness chain and the `LOW_POWER_ENABLE` lever are described in
[Display.md](../../Docs/Display.md#low-brightness). Only the host drives the
pin (`PB8`), through `LowBrightness::set()`/`toggle()` in
`User/Src/Display/LowBrightness.cpp`:

- `display low on|off` on the console sets it and saves it (settings tag
  `DisplayFlags`, [Settings.md](Settings.md#tag-registry));
  `AstroWeather_Init()` applies the saved state before the local board starts.
  `display low` reports the state in use and the saved one; see
  [Console.md](Console.md#display).
- Switch 2 toggles it, logs `Low brightness on` or `off`, and saves it from
  `MainLoopTask`. `Settings::Store::save()` holds the store's lock, so a press
  cannot interleave its EEPROM page writes with a console save. The switch is
  debounced in hardware only (`R306`, `R308`, `C308`: τ ≈ 1 ms on press, 2 ms
  on release, into a Schmitt-trigger input); a longer bounce toggles twice.

Measured on the host board at 5 V with all LEDs lit (`8.888` on every numeric
display, the full matrix), alternating the two states in the same light:

| State | Board current | LEDs (minus the 14 mA with all LEDs off) |
| --- | --- | --- |
| Normal | 73–79 mA | 59–65 mA |
| Low | 42 mA | 28 mA |

Low brightness cuts the LED current to about 0.45 of normal, the board current
by about 45 %. The divider alone predicts 0.32–0.42 depending on the light, so
the current-set stages do not scale exactly with `LED_BRIGHTNESS`, and the ratio
differs in other light.

## Lifecycle

`AstroWeather.cpp` creates, as file-scope statics:

- `localBoard`, the `PcbDisplayBoard` on `localSct` (SPI3 DMA) and TIM2;
- `remoteBoard10` to `remoteBoard15` on `i2c1Bus`;
- `display`, holding the local board and the six remote boards.

`AstroWeather_Init()` sets the host's address from its straps
(`setLocalAddress()`), applies the saved low brightness, puts the "no data"
state into the local board, shows the first slot-test frame and starts the
refresh (`localBoard.start()`). `MainLoopTask` then runs the boot screens, after
which the local board shows its own state: "no data" on numerics 0 and 1 and
the matrix until the first astro refresh, with the current readout and the
clock on numerics 2 and 3. See [Architecture.md](Architecture.md#init-order).

## Tests

The shared display code is tested in `../Common/tests`
([Display.md](../../Docs/Display.md#tests)). On the host,
`AstroDisplayMapperTests` and `AstroProgressBarTests` cover what the refresh
writes. Not covered natively: `Display`, `BufferedDisplayBoard` and
`LowBrightness`.
