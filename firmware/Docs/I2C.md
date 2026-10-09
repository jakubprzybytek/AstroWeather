# I2C

The I2C bus that links the host to its settings EEPROM and to the remote
display boards, and everything both firmware images do on it. The message
format and the address straps are shared code in [`../Common`](../Common/README.md);
the host's master side is in [HostControllerA](../HostControllerA/README.md), the
display board's target side in [DisplayController](../DisplayController/README.md).

## Bus

| | |
| --- | --- |
| Peripheral | I2C1 on both boards, `PA9` SCL, `PA10` SDA, 7-bit addressing |
| Speed | 100 kHz standard mode (`Timing` `0x00503D58` from the 16 MHz PCLK), analog filter on, digital filter off |
| Pull-ups | 2.2 kΩ to 3V3 on SCL and SDA, fitted once on the bus |
| Wiring | Daisy-chained through `J102`/`J104` together with power, `LED_BRIGHTNESS` and `LOW_POWER_ENABLE` |
| Controller | The host, the only master |

The pull-ups are sized for the eventual bus of one EEPROM and up to ten
display boards, roughly 300-450 pF, where the common 4.7 kΩ would exceed the
1 us rise time 100 kHz allows. Without pull-ups the lines never return high,
the peripheral latches BUSY on its first START and no device responds. They are
`dnp` in the schematic ([Hardware review](../../KiCad/Docs/Hardware_Review.md) H-4).

### Devices

| Address | Device |
| --- | --- |
| `0x00` | General call: the host's timeline sync, taken by every display board ([Timeline sync](#timeline-sync)) |
| `0x08` | The host's own address in CubeMX; unused, since the host is never addressed. `eeprom scan` skips it |
| `0x10`-`0x15` | Display boards, one per forecast block ([Addresses and straps](#addresses-and-straps)) |
| `0x50`-`0x57` | The 24AA04 settings EEPROM. The SOT-23 part has no address pins and uses the low bit as the block select, so it answers on all eight; treat them as reserved. See [Settings.md](../HostControllerA/Docs/Settings.md#storage-medium) |

`eeprom scan` on the host console lists every device that answers.

## Messages

Every message to a board's own address is 36 bytes: one command byte and one
35-byte plane in the logical board layout
([Display.md](Display.md#logical-board-buffer)). The timeline sync, the one
other message, goes to the general-call address
([Timeline sync](#timeline-sync)).

| Offset | Size | Content |
| ---: | ---: | --- |
| 0 | 1 | Command |
| 1 | 35 | The content, or one attribute plane in the same layout |

| Command | Plane | Receiver |
| ---: | --- | --- |
| `0x01` | Content | Replaces the content, applies the staged attributes, shows the board |
| `0x02` | `BoardAttributes::blink` | Staged |
| `0x03` | `BoardAttributes::level0` | Staged |
| `0x04` | `BoardAttributes::level1` | Staged |

The plane is serialized as numeric display 1, numeric display 2, matrix rows 0
to 4, numeric display 3, numeric display 4: five segment bytes per numeric
display, three bytes per matrix row, little-endian with bit 0 in the first
byte's least significant bit, bits 21-23 zero.

The host sends a board its three attribute planes first, `0x02`, `0x03`,
`0x04`, then the content, `0x01`, each message about 3.6 ms at 100 kHz. The
board stages the attribute planes and applies them with the next content, so
content and attributes always change together. Staged attributes stay until
replaced: a host that sends only content keeps the last attributes, and one
that never sends any gets full brightness and no blinking. An unknown command
or a wrong length is rejected and changes nothing. There is no reply message;
the I2C ACK/NACK is the only acknowledgement.

`serializeI2c()`, `serializeAttributesI2c()`, `deserializePlaneI2c()` and the
content-only `deserializeI2c()` are in `DisplayI2cProtocol.cpp`.

### Timeline sync

The host broadcasts its refresh timeline's position to the general-call
address `0x00`, so every display board takes it at the same instant; what the
boards do with it is in [TimelineSync.md](TimelineSync.md).

| Offset | Size | Content |
| ---: | ---: | --- |
| 0 | 1 | `0x05` (`kSyncCommand`) |
| 1 | 4 | The host's frame number, little-endian |
| 5 | 2 | Microseconds into that frame, little-endian |

A board takes command `0x05` only on the general call, never on its own
address, and rejects a general-call write that is not a whole 7-byte sync
message (`deserializeSync()`). The host stamps its timeline just before the
START; `kSyncTransferMicros`, 130 us, is the time from that stamp to the
board's address-match interrupt, where the board stamps its own: the HAL's
setup, START and the address byte at 100 kHz, and the interrupt's entry. The
data bytes come after both stamps, so an interrupt that holds the host up
between them (its refresh interrupt runs up to 850 us) moves neither. A broadcast that no board acknowledges, as with
no display board on the bus, is not an error.

### Status read

A one-byte read from a board's own address answers its status: `0xA0`
(`kStatusTag`) with two flags (`boardStatus()`).

| Bit | Set | Clear |
| --- | --- | --- |
| 0, `kStatusLocked` | On the host's timeline; no burst needed | Wants a burst of syncs: after a reset, a jump or an HSITRIM step, until a sync has measured its rate |
| 1, `kStatusNeedsContent` | Wants its content: from boot until the first content message, and again once its content has expired (7 h, `kContentLifetimeMs`) | Showing content the host sent |

So a board answers `0xA2` just after a reset and `0xA1` once settled. `0x00`
is a board whose firmware has neither. A host that knew only `0xA0` and
`0xA1` reads `0xA2` and `0xA3` as neither, so it starts no burst for a board
with this firmware: host and boards are updated together.

`serializeSync()`, `deserializeSync()`, `boardStatus()` and the
`statusValid()`, `statusWantsSyncs()` and `statusNeedsContent()` tests are in
`DisplayI2cProtocol`.

## Addresses and Straps

Every board, the host included, has three address straps, `ADDR_0` (`PB10`),
`ADDR_1` (`PB11`) and `ADDR_2` (`PB14`). Each can be left floating (0), tied to
ground (1) or tied to VCC (2), which gives 27 board IDs; the address is
`0x10 + id`, `0x10` to `0x2A`, so a board with no straps fitted is `0x10`.

`Display::detectBoardAddress()` (`DisplayAddress.cpp`) reads each pin twice:

1. As an input with the internal pull-down: high means VCC, state 2.
2. Otherwise with the internal pull-up: high means floating, state 0; low means
   ground, state 1.

The pins are then left in analog mode, so a strap tied to VCC draws no pull
current, and `id = ADDR_0 + 3 × ADDR_1 + 9 × ADDR_2`.

The address decides what a board shows: forecast block *n* goes to the board at
`0x10 + n` (`BoardChain.hpp`, chain positions 0 to 5). The host's own display is
not special; it shows the block of its own address, block 0 with no straps.
The host sends to every other address of the chain and skips its own. A host
strapped outside the chain shows no block. Boards boot showing their address
([Display.md](Display.md#boot-screens)), and the host's `status` probes the
chain (`remote` line).

## Host Side

The host is a polled, blocking master: `HAL_I2C_Master_Transmit()`,
`HAL_I2C_Master_Receive()`, `HAL_I2C_Mem_Read/Write()` and
`HAL_I2C_IsDeviceReady()` with timeouts, no I2C interrupt and no DMA.

### `Device::I2cBus`

`I2cBus` (`User/Src/Device/I2cBus.cpp`) owns `hi2c1` and a mutex held for one
transfer at a time. The EEPROM driver and the display boards are driven from
different tasks (console, refresh, `MainLoopTask`, `DisplaySync`), so every
client of the bus must go through the same `I2cBus`, never the raw handle. The
clock and current-sense tasks touch only the local board and never use I2C.

Besides `transmit()`, `receive()` and the EEPROM's `memRead()`/`memWrite()`,
`transmitFilled()` sends a message that carries the instant it goes out: once
the bus is free it fills the message with interrupts off, then transmits with
the scheduler suspended, so no task runs between the fill and the START. The
scheduler is suspended inside the critical section, so no tick is held back
before the fill reads the time. The 7-byte sync takes about 1 ms.

### `BufferedDisplayBoard`

One per chain address (`User/Src/Display/BufferedDisplayBoard.cpp`). It keeps
the board's content and attributes in RAM and on `submit()` sends the three
attribute messages and the content, stopping at the first failure.

| | |
| --- | --- |
| Transfer timeout | 50 ms per message (`kTransferTimeoutMs`); the HAL times the whole transfer, so a task kept off the CPU that long also fails it |
| Probe | `present()`: `HAL_I2C_IsDeviceReady()`, one trial, 5 ms |
| Retry | A board that fails but still answers a probe gets the whole submit again, up to 3 attempts 5 ms apart; an absent board is not retried |
| `lastSubmitOk()` | Whether the last submit reached the board |

Logging, through the host's log and error log:

- `DisplayBoard 0x11 sent on attempt 2, first error=0x...`, a warning, when a
  retry was needed.
- `DisplayBoard 0x14 unreachable status=1 error=0x... attempts=n` when it
  failed: once on the transition, then every 30 s while it keeps being sent to.
  Restating it matters for a board missing from boot, which fails before USB
  has enumerated.
- `DisplayBoard 0x11 online` when it answers again.

The error is the HAL's `HAL_I2C_ERROR_*` bits: `0x04` NACK, `0x01` bus error,
`0x02` arbitration lost, `0x20` timeout.

### When the boards are sent to

Remote boards are written only by `Display::submit()`, after an astro refresh
(every board) and by the console's `display` commands
(`display 0x12 ...` one board, `display all ...` every board, via
`Display::submitRemote()`); see
[Console.md](../HostControllerA/Docs/Console.md#display). The current readout,
the clock and the progress bar change only the local board and send nothing,
so an unreachable board is reported when something actually tries to reach it.

Separately, `DisplaySyncTask` broadcasts the timeline sync and reads each
remote board's status byte on its own schedule
([TimelineSync.md](TimelineSync.md#syncs-from-the-host)), with a 5 ms timeout per
transfer; neither is logged as a failure. A board that answers "needs
content" gets its state sent again at once, through `Display::resendRemote()`
([host Display.md](../HostControllerA/Docs/Display.md#the-display-aggregate)).

## Display Board Side

`I2cTarget` (`DisplayController/User/Src/I2cTarget.cpp`) owns `hi2c1` in target
mode, with the HAL's interrupt-driven sequential listen API:

- `HAL_I2C_AddrCallback()`: the host addressed this board, or the general
  call. A general-call write starts a 7-byte receive into the sync buffer. A
  write to the board's own address starts a 36-byte receive
  (`I2C_FIRST_AND_LAST_FRAME`) and flashes `LED_2` for 20 ms. A read gets one
  byte, the status from the flags `TimelineFollower` (locked) and `DisplayApp`
  (needs content) last set ([Status read](#status-read)).
  `LED_2` therefore shows the data traffic only: neither the sync broadcast
  nor the status reads flash it.
- `HAL_I2C_SlaveRxCpltCallback()`: for a sync, all 7 bytes arrived. The
  interrupt stamps the board's timeline first (`PcbDisplayBoard::stampNow()`),
  keeps the message with the stamp, a newer one replacing one not yet taken,
  and wakes `DisplayApp` with `kFlagSync`. For a message, all 36 bytes
  arrived. They are queued, four deep (the host sends four messages a few ms
  apart; when full the oldest is dropped and counted), and `DisplayApp` is
  woken with `kFlagFrame`.
- `HAL_I2C_ListenCpltCallback()`, `HAL_I2C_ErrorCallback()`: the transfer
  ended. A write shorter than 36 bytes counts as a probe (no data, as the
  host's `status` sends) or a short write, a general-call write shorter than
  7 bytes as a short sync; listening restarts.

Nothing is decoded in the interrupt. `DisplayApp` feeds each queued message to
`FrameAssembler` (`User/Src/FrameAssembler.cpp`), which decodes it with
`deserializePlaneI2c()`, stages attribute planes and applies them with the
content, and each sync to `TimelineFollower`
([TimelineSync.md](TimelineSync.md#following-the-host)).

General call is enabled in `I2cTarget::begin()` (`Init.GeneralCallMode`), when
it re-initialises I2C1 with the strap address; the CubeMX `.ioc` keeps it
disabled ([Development.md](Development.md#settings-that-must-survive-regeneration)).

The HAL NACKs any byte after the 36th, so a longer write fails on the host.
Listening restarts after every error, and `DisplayApp` checks every second that
the peripheral is still listening (`ensureListening()`), which also recovers
from a bus error the HAL left in the ready state.

Listening starts only once the straps are read, after I2C1 is re-initialised
with the strap address, so a board never answers on another board's address.
Until then, for a few milliseconds after reset, CubeMX's placeholder own
address `0x10` is set but not serviced; a host write in that window is
acknowledged and held until the host's 50 ms timeout.

### Interrupt priority

The display board's I2C1 interrupt has priority 1, above the refresh timer,
the display DMA and the switches at 3
([DisplayController Architecture](../DisplayController/Docs/Architecture.md#interrupt-priorities)).
The refresh interrupt runs up to about 170 us, and the host starts the next
message about 100 us after a STOP. The HAL sets `CR2.NACK` when it handles a
STOP, and software cannot clear that bit; if the STOP is handled only after the
host's next address has matched, the NACK stays set and the board refuses that
message's second byte (error `0x04` on the host). At priority 1 the STOP is
always handled in time. `stopWithAddrPending` in `g_displayStats` counts any
STOP handled late and should stay 0.

The host needs no I2C interrupt priority: it polls.

### Diagnostics

The display board counts frames and attributes accepted, rejected messages,
short writes, probes, bus errors, listen restarts, queue overruns, the last
HAL error, late STOPs and the syncs received, short, rejected and applied in
`g_displayStats`, read over SWD; see
[DisplayController Architecture](../DisplayController/Docs/Architecture.md#diagnostics).
On the host, `status` (`remote` line) probes each chain address, `time sync`
shows each board's answer to the last status read, and the error log keeps the
unreachable warnings.

## Tests

| Suite | Covers |
| --- | --- |
| `Common/tests/DisplayI2cProtocolTests.cpp` | The 36-byte layout, the round trip of content and attribute planes, masking of bits 21-23, rejection of short, long and null messages and unknown commands without touching the destination; the sync message's layout, round trip and rejection |
| `Common/tests/DisplayAddressTests.cpp` | All 27 strap combinations through the stub GPIO, the pins left analog without pull, `boardAddress()` limits, `detectBoardAddress()` |
| `DisplayController/tests/FrameAssemblerTests.cpp` | The staging rules on the receiving side |

Not covered natively: `I2cBus`, `BufferedDisplayBoard`, `DisplaySyncTask` and
`I2cTarget`, which need the HAL; they are checked on the bench with the
counters above.
