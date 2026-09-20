# Persistent Settings

## Overview

The Host Controller keeps a small set of user-configurable settings in an
on-board EEPROM so that they survive a power cycle. Settings are read once
during startup and applied to the owning tasks before the scheduler runs, and
they are written back immediately whenever a console command changes one.

Currently persisted:

- Current-sense logging on/off (`adc log`).
- Current-sense display output on/off (`adc display`).
- WiFi SSID and password.

The storage format is designed so that adding a further setting requires no
format version change and no migration code. That property is the main subject
of this document, and the rules that preserve it are listed under
[Adding a new setting](#adding-a-new-setting).

## Storage Medium

| Property | Value |
| --- | --- |
| Part | Microchip 24AA01T-I/OT |
| Bus | I2C1, shared with the remote Display Controllers |
| Device address | `0x50`, 7-bit |
| Capacity | 128 bytes |
| Page size | 8 bytes |
| Write cycle | 5 ms maximum |

The part has no address pins, so it acknowledges the whole range `0x50`–`0x57`.
Treat all eight addresses as reserved when assigning addresses to other devices
on the bus.

Access goes through `Device::Eeprom24AA01`, which splits writes on page
boundaries and polls for the internal write cycle. That driver in turn uses
`Device::I2cBus`, which holds the mutex serializing all I2C1 traffic.

## Image Layout

The whole 128-byte array is one record. A fixed header carries integrity
information, and the payload after it is a sequence of variable-length records.

```
offset  size  field
0x00    2     magic       'A' 'W'
0x02    2     crc16       big endian, see below
0x04    1     version     container format version, currently 1
0x05    1     payloadLen  number of payload bytes that follow
0x06    N     payload     sequence of tag/length/value records
0x06+N  ...   padding     0xFF to the end of the array
```

The CRC deliberately sits *before* the fields it protects so that everything it
covers is a single contiguous range, which keeps the implementation to one call.

### CRC

CRC-16/CCITT-FALSE:

| Parameter | Value |
| --- | --- |
| Polynomial | `0x1021` |
| Initial value | `0xFFFF` |
| Input reflected | no |
| Output reflected | no |
| Final XOR | none |

It covers `version`, `payloadLen` and the payload, that is bytes `0x04` through
`0x05 + payloadLen`. It does **not** cover the magic or the padding.

Implemented in software rather than using the STM32 CRC peripheral, so that the
feature needs no `.ioc` change. See [CubeMXCompliance.md](CubeMXCompliance.md).

### Payload records

Each record is:

```
tag     1 byte
length  1 byte    number of value bytes, 0x00..0xFE
value   `length` bytes
```

Records run consecutively with no alignment or padding. A tag of `0xFF`
terminates the sequence. That value is chosen because an erased EEPROM byte
reads `0xFF`, so a blank or over-short payload terminates naturally rather than
needing an explicit end marker to be written.

## Tag Registry

This table is the authoritative allocation. `User/Inc/Settings/SettingsCodec.hpp`
must match it.

| Tag | Name | Length | Value |
| --- | --- | --- | --- |
| `0x01` | `AdcFlags` | 1 | Bit 0 = current-sense logging enabled, bit 1 = current-sense display enabled. Remaining bits reserved, write 0. |
| `0x10` | `WifiSsid` | 1–32 | SSID bytes, not NUL terminated. |
| `0x11` | `WifiPassword` | 1–63 | Passphrase bytes, not NUL terminated. |
| `0xFF` | *reserved* | — | End of records. Never allocate. |

Suggested grouping for future allocations, to keep related settings together:

| Range | Use |
| --- | --- |
| `0x01`–`0x0F` | Local device behaviour |
| `0x10`–`0x1F` | Network and credentials |
| `0x20`–`0x2F` | Display behaviour |
| `0x30`–`0xFE` | Unallocated |

## Adding a new setting

The format is built so this is routine. Adding a setting does **not** change
`version` and does **not** need migration code, because content written before
the setting existed simply has no record for the new tag, and the decoder leaves
that field at its compile-time default.

1. Allocate the next free tag in the appropriate range and add a row to the
   [Tag Registry](#tag-registry) above. Never reuse a retired tag.
2. Add the enumerator to `Settings::Tag` in
   `User/Inc/Settings/SettingsCodec.hpp`.
3. Add the field to `Settings::Values`, **with a default that matches the
   behaviour the firmware has when nothing is stored**. See
   [Defaults](#defaults).
4. In `encode()`, append the record. If the setting is at its default or
   otherwise unset, prefer to write no record at all; that is what keeps unused
   settings free. See [Space budget](#space-budget).
5. In `applyRecord()`, handle the new tag. Validate `length` before reading the
   value; a record on the chip may be any length.
6. Apply the value at startup in `AppVariant_Init()`, before the owning task is
   started.
7. Persist it from whichever console command changes it, by updating
   `store.values()` and calling `store.save()`.
8. Add native test cases to `tests/SettingsCodecTests.cpp`. At minimum a round
   trip, and a case proving an image written *without* the new record still
   decodes and leaves the new field at its default.
9. Check the worst-case size still fits. See [Space budget](#space-budget).

### When the version must change

Bump `kContainerVersion` only if the **container** changes, not when settings
are added or removed. That means a change to the header layout, the CRC
algorithm or coverage, or the record framing itself. Such a change should be
rare. A decoder that meets a version it does not recognise reports
`BadVersion` and falls back to defaults, so an unexpected bump silently discards
the user's settings.

### Invariants

These must hold for the compatibility rules above to work.

- **Tag numbers are permanent.** Once assigned, a tag never changes meaning.
  Retire a setting by abandoning its tag, never by recycling it.
- **Unknown tags are skipped, not rejected.** This lets an older build read a
  chip written by a newer one.
- **An absent record means "use the default"**, never "invalid".
- **Decoding always leaves `Values` fully populated**, on every exit path,
  including failures.

## Decode Results

`Settings::decode()` reports one of the following. In every case other than
`Ok`, `Values` holds the compile-time defaults.

| Result | Meaning | Typical cause |
| --- | --- | --- |
| `Ok` | Image parsed | — |
| `Blank` | First two bytes are `0xFF` | Never-written chip |
| `BadMagic` | Magic is neither `AW` nor erased | Foreign data, wrong offset |
| `BadVersion` | Container version not understood | Written by newer firmware |
| `BadLength` | `payloadLen` exceeds the array | Corruption |
| `BadCrc` | CRC mismatch | Corruption, torn write |
| `Truncated` | A record runs past `payloadLen` | Corruption |

`Settings::Store::load()` collapses these into `Ok`, `Defaulted` or
`ReadFailed`, and keeps the detailed result available through `lastDecode()`
for logging. `settings show` reports it as `boot-load=`, which describes what
was found at startup and not the chip's present content.

A failure is never fatal: the firmware boots on defaults, and the next save
overwrites the bad image.

## Defaults

Defaults live in `Settings::Values` and must match the behaviour of the owning
code when nothing is stored. If the two disagree, a blank chip behaves
differently from a chip holding the default value, which is a difficult class of
bug to spot.

| Field | Default | Must match |
| --- | --- | --- |
| `adcLogEnabled` | `false` | `CurrentSenseTask::loggingEnabled_` |
| `adcDisplayEnabled` | `true` | `CurrentSenseTask::displayEnabled_` |
| `wifiSsid` | empty | — |
| `wifiPassword` | empty | — |

## Space budget

Available payload is `128 - 6 = 122` bytes. Each record costs its value length
plus two bytes of framing.

| Content | Payload cost |
| --- | --- |
| `AdcFlags` | 3 |
| WiFi, typical (15-char SSID, 20-char password) | 39 |
| WiFi, worst case (32 + 63) | 99 |
| **Worst case total** | **102 of 122** |

A measured image on hardware with SSID `AstroNet` and a 13-character password
occupied 34 bytes of 128, leaving 94 free. With no WiFi configured the image is
9 bytes.

This is why the payload is TLV and not a packed struct. A struct would have to
reserve the worst case for credentials whether or not any were set, permanently
consuming 99 of the 122 available bytes and leaving almost nothing for later
additions.

When adding a setting, check the worst case still fits, and prefer omitting a
record over writing a default value.

## Write Behaviour

`Settings::Store::save()` encodes the full 128-byte image, reads the current
contents, and writes only the 8-byte pages that differ. Toggling a single flag
therefore costs one page and one write cycle, roughly 5 ms, rather than 16 pages.
If the read fails, every page is written rather than skipping the save.

The encoder always produces a full-length image with `0xFF` padding, so the
result is deterministic and no stale bytes are left behind a shortened payload.

Saves happen synchronously from the console task whenever a setting changes.
The 24AA01 is rated for 1 million write cycles, so per-change saves are not a
wear concern at console-command rates.

## Console Interface

| Command | Effect |
| --- | --- |
| `settings show` | Print current values and the startup load result. The password is reported only as `<set>` or `<unset>`. |
| `settings save` | Force a write. |
| `settings defaults` | Reset all values to defaults and save. |
| `wifi set <ssid> <password>` | Store credentials and save. |
| `wifi clear` | Drop stored credentials and save. |
| `adc log on\|off` | Toggle and save. |
| `adc display on\|off` | Toggle and save. |

The raw image can be inspected with `eeprom dump` and `eeprom read`, which is
the quickest way to confirm a new record encodes as intended. See
[Development.md](Development.md#useful-commands).

## Code Layout

| File | Responsibility |
| --- | --- |
| `User/Inc/Settings/SettingsCodec.hpp` | Layout constants, `Tag`, `Values`, `DecodeResult` |
| `User/Src/Settings/SettingsCodec.cpp` | `encode()`, `decode()`, `crc16()`. Pure, no HAL |
| `User/Inc/Settings/SettingsStore.hpp` | `Store`, `LoadResult` |
| `User/Src/Settings/SettingsStore.cpp` | EEPROM read/write, page diffing |
| `User/Src/Console/SettingsCommand.cpp` | `settings` and `wifi` commands |
| `tests/SettingsCodecTests.cpp` | Native tests |

The codec is deliberately free of HAL dependencies so it builds and runs on the
host. Keep it that way: all EEPROM access belongs in `SettingsStore`.

### Tests

`tests/SettingsCodecTests.cpp` runs under the `NativeTests` preset alongside the
other native suites. It currently covers round trip, blank chip, a corrupted
byte, an unknown tag between known ones, an absent record, a record running past
the payload, an unrecognised container version, worst-case field lengths, and
the empty-WiFi case.

The unknown-tag and absent-record cases are what pin down the compatibility
rules. Do not delete them.

## Startup

`Settings::Store::load()` is called from `AppVariant_Init()`, which runs before
`osKernelStart()`. This is safe because EEPROM reads take no `osDelay` and the
I2C bus mutex is uncontended at that point, so acquiring it takes the
non-blocking path.

The outcome is logged, but not the values, since they include credentials. Note
that this log line is emitted before USB CDC has enumerated, so it is only
visible to a debugger attached over SWD. Use `settings show` to inspect the
loaded state from the console.

## Limitations

- **A torn write is detected but not recoverable.** Losing power during a save
  leaves a CRC mismatch, and the firmware falls back to defaults. Surviving
  this would need two slots with a sequence number, which does not fit: 122
  usable bytes split in two gives 61 per slot against a 102-byte worst case.
  A larger part such as the 24AA02 or 24AA08 is the answer if this ever
  matters, rather than capping the passphrase length to force redundancy in.
- **Credentials are stored and transported in the clear.** `wifi set` is echoed
  to the log like any other console line, and `eeprom dump` prints the stored
  password. `settings show` masks it, but that is the only place it is hidden.
- **Unknown tags are not preserved across a save.** The image is re-encoded from
  the in-RAM `Values`, so any record this firmware does not recognise is
  dropped. Flashing an older build, saving, then flashing a newer one loses the
  newer settings.

## Worked Example

An image holding logging enabled, display disabled, SSID `AstroNet` and password
`hunter2secret`, read back with `eeprom read 00 30`:

```text
00: 41 57 3F 90 01 1C 01 01 01 10 08 41 73 74 72 6F
10: 4E 65 74 11 0D 68 75 6E 74 65 72 32 73 65 63 72
20: 65 74 FF FF FF FF FF FF FF FF FF FF FF FF FF FF
```

| Bytes | Meaning |
| --- | --- |
| `41 57` | Magic `AW` |
| `3F 90` | CRC-16 over the following 30 bytes |
| `01` | Container version 1 |
| `1C` | 28 payload bytes |
| `01 01 01` | `AdcFlags`, 1 byte, bit 0 set: logging on, display off |
| `10 08 41 73 74 72 6F 4E 65 74` | `WifiSsid`, 8 bytes, `AstroNet` |
| `11 0D 68 ... 74` | `WifiPassword`, 13 bytes |
| `FF ...` | Padding |

After `settings defaults`, the same array reads:

```text
00: 41 57 04 C2 01 03 01 01 02 FF FF FF FF FF FF FF
```

The WiFi records are gone entirely rather than being present and empty, leaving
a 3-byte payload.
