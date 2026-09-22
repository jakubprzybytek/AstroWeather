# ST67 Bypass - Maintainer Notes

Internal reference for anyone extending, fixing, or re-validating this
project. User-facing build/usage instructions are in the root
[README.md](../README.md). Full design rationale and the staged validation
plan are in [ST67_Bootloader_Mode_Implementation_Plan.md](ST67_Bootloader_Mode_Implementation_Plan.md).
[ST67_Bypass_Bootloader_Project_Guide.md](ST67_Bypass_Bootloader_Project_Guide.md)
is the original bring-up guide; parts of it (bootloader straps "not yet
specified", STM32G0B1 references) are now stale and should be reconciled with
this document once someone has time.

## Source layout

- `Core/Src/main.c` - CubeMX-generated startup; only the `USER CODE` blocks
  call into project code (`Bridge_Init`, `ST67_EnterMode`, `Bridge_Process`).
  Required init order: GPIO (with `CHIP_EN` low) -> USART2 -> `Bridge_Init`
  -> USB device init -> `ST67_EnterMode` -> foreground `Bridge_Process` loop.
- `User/Src/bridge.c` / `User/Inc/bridge.h` - the transport. Two
  single-producer/single-consumer ring buffers (USB->UART, UART->USB),
  ISR-driven UART RX, deferred (foreground-only) ST67 reset on a CDC DTR
  rising edge, and the two status LEDs. Sizes are in
  `User/Inc/bridge_config.h` (`USB_TO_UART_RING_SIZE`, `UART_TO_USB_RING_SIZE`,
  `USB_TX_BUFFER_SIZE`) - must stay powers of two, ring indexing uses bit
  masks.
- `User/Src/st67_mode.c` / `User/Inc/st67_mode.h` - owns GPIO strap levels and
  reset timing only (`CHIP_EN`, `CS`, `BOOT`). Never touches USART2/USB/ring
  state. `bridge.c` only asks it to re-enter the configured mode.
- `USB_Device/App/usbd_desc.c` - USB identity string
  (`USBD_PRODUCT_STRING = "AstroWeather ST67 Bypass"`), used to find the
  right COM port without guessing.
- `tools/` - host-side scripts (see below).

## Mode selection

`ST67_DEFAULT_MODE` is a compile-time macro (`ST67_DEFAULT_MODE_MANUFACTURE`
or `ST67_DEFAULT_MODE_BOOTLOADER`), set from the CMake cache variable
`ST67_DEFAULT_MODE` (`MANUFACTURE`/`BOOTLOADER`) in `CMakeLists.txt`, wired to
four `CMakePresets.json` presets. Any other value fails the build via a
`#error` in `st67_mode.h` and a `FATAL_ERROR` in `CMakeLists.txt` - this is
intentional, verified by configuring with a bogus value.

Preprocessor `#if` cannot compare C enum identifiers, which is why the header
keeps separate numeric `ST67_DEFAULT_MODE_*` macros alongside the `St67Mode`
enum used at runtime.

## Reset/session-boundary handling

`Bridge_OnHostConnectionChanged` sets a pending-reset flag only on a
false->true DTR transition; the actual `HAL_Delay`-heavy `ST67_EnterMode`
call always runs from `Bridge_Process` (foreground), never from the USB
interrupt. Before re-entering the configured mode, `bridge_reset_transport_state`:

- waits up to `USB_TX_DRAIN_TIMEOUT_MS` (50 ms) for an already-submitted USB
  IN transfer to finish, so a stale packet doesn't linger into a new host
  session (counted via `usb_tx_stale_flush_count` if the timeout is hit);
- flushes both software rings;
- drains any pending USART RX data register content;
- clears UART framing/noise/parity/overrun flags.

Diagnostic counters (all file-local `static volatile` in `bridge.c`,
inspectable via debugger - intentionally not exposed through the CDC stream):
`usb_to_uart_overflow_count`, `uart_to_usb_overflow_count`,
`uart_hardware_overrun_count`, `uart_framing_error_count`,
`uart_noise_error_count`, `uart_parity_error_count`, `usb_tx_failure_count`,
`usb_tx_stale_flush_count`, `st67_session_reset_count`. Check them with a
debugger after any transport stress test.

**`usb_tx_failure_count` is not a data-loss indicator and does not need to be
zero.** `bridge_uart_to_usb` only advances `uart_to_usb_tail` (i.e. discards
bytes from the ring) after `CDC_Transmit_FS` returns `USBD_OK`; on failure it
just resets `usb_tx_busy` and retries the same bytes next loop iteration.
Confirmed 2026-08-19: a full 4 MiB debugger-instrumented read logged
`usb_tx_failure_count = 1131` and `uart_framing_error_count = 1`, yet
re-hashing that exact dump (boot2, partition table, and the active FW slot)
matched the vendor files byte-for-byte. The counters that actually indicate
lost/corrupted bytes are the ring overflow counters
(`usb_to_uart_overflow_count`, `uart_to_usb_overflow_count`) and the
hardware UART error counters (`uart_hardware_overrun_count`,
`uart_framing_error_count`, `uart_noise_error_count`,
`uart_parity_error_count`) - those should be 0, or if not, the dump they
came from should be independently re-hashed against a known-good image
before trusting it, since a SHA-256 match is stronger evidence than a clean
counter reading.

## LED semantics

`LED_1` (`PC13`) = TX-to-ST67 activity, `LED_2` (`PB9`) = RX-from-ST67
activity, both stretched to a minimum 60 ms on-time
(`LED_MIN_ON_MS`) so short bursts stay visible. CubeMX initializes `LED_1`
high (on) at boot; `bridge_update_tx_led` deliberately leaves it alone
(`tx_led_activity_seen` guard) until the first real byte is forwarded, so it
reads as "alive at boot" instead of being immediately clobbered by a stale
recent-activity timestamp during the multi-hundred-ms `ST67_EnterMode` delay
that runs before the foreground loop starts.

## Build toolchain on a machine without it on PATH

This dev machine has no `cmake`/`ninja`/`arm-none-eabi-gcc` on `PATH` by
default, but STM32CubeIDE bundles all three. Example (adjust the version
folder name):

```bash
export PATH="/c/Program Files/ST/STM32CubeIDE_2.2.0/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740/tools/bin:/c/Program Files/ST/STM32CubeIDE_2.2.0/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.cmake.win32_1.1.200.202605190741/tools/bin:/c/Program Files/ST/STM32CubeIDE_2.2.0/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.ninja.win32_1.1.200.202606260906/tools/bin:$PATH"
```

All four presets have been built clean with this toolchain with no warnings
in project-owned files.

## Host tooling (`tools/`)

- `Program-ST67.sh` / `Program-ST67.ps1` - the real programming path, wraps
  `QConn_Flash/QConn_Flash_Cmd.exe`. Validates the SDK root, exactly one
  matching QConn executable, the flash config template and every image it
  references, the selected/newest firmware version, and the efuse image,
  before generating a temporary absolute-path config and invoking QConn.
  Requires `--force` to actually run QConn; otherwise prints the lock-risk
  warning and exits 1. Propagates QConn's exact exit code on failure, and
  also treats a `Parse para error`/`ErrorCode:`/Python traceback in QConn's
  own output as a failure even when it exits 0 (it does, for some error
  paths). The bash version writes native Windows paths (`cygpath -w`) into
  the generated ini - QConn is a native Windows program that reads that
  file itself, so MSYS's argv path translation never applies to paths
  embedded inside it; a raw `/d/...` path gets misread as "root of current
  drive, folder literally named d".
- `Query-ST67.sh` - non-destructive efuse/chip-info read via
  `NCP_info/QConn_Eflash.exe -r --efuse --start=0x0 --end=0x1ff` +
  `read_chip_info`. Never needs `--force`. This is the right first check
  before ever running `Program-ST67.sh --force`.
- `Dump-ST67-Flash.sh` - bulk flash read-back for manual inspection. It uses
  `QConn_Flash_Cmd.exe --read`, which does not enforce the strict flash-type
  match applied by `QConn_Eflash.exe -r --flash`; it logs "flash config Not
  found, use default" and proceeds across boards with different flash chips.
- All scripts validate `--port` before touching the SDK (checked against
  `[System.IO.Ports.SerialPort]::GetPortNames()` on Windows, or file
  existence on Linux) and clean up any generated temp files in a
  `trap ... EXIT`.

### Vendor tool gotchas (learned the hard way)

- `QConn_Eflash.exe -a`/`--auto` means **"auto burn"** - an autonomous
  erase+program+verify cycle - not "auto-detect flash parameters for a
  read". Passing it with a read-only request causes an infinite
  connect/handshake/fail/restart loop (confirmed live: it never gives up on
  its own). Never pass `-a` for a plain read.
- Overriding `flash_id` in a private copy of
  `NCP_info/chips/qcc743/eflash_loader/eflash_loader_cfg.ini` and passing it
  via `-c` does **not** fix the `QConn_Eflash.exe --flash -r` flash-type
  mismatch - the check appears to use something else internally (possibly
  the binary `flash_para.bin` blob or a hardcoded table). Don't spend more
  time on this; it's out of scope to reverse-engineer, and
  `QConn_Flash_Cmd.exe --read` already works fine instead.
- The loader config's `verify` setting (`0` = "verify by calculating
  SHA256(xip)", `>0` = "read back and verify via SHA256(sbus)") means real
  programming already includes a built-in verification step regardless of
  which value is configured - `0` is not "no verification".

## Verified ST67 flash map (and the A/B slot trap)

Confirmed 2026-08-18 against a full 4 MiB read-back of a board programmed
with `Program-ST67.sh --port COM4 --profile MissionT02 --force`:

| Region | Address | Content |
|---|---|---|
| boot2 | `0x000000` | `st67w611m_boot2_v8.1.9.bin` (exact match) |
| partition table | `0x00E000` | `partition.bin` (exact match) |
| FW slot A (active) | `0x010000` | the programmed profile image (exact match) |
| FW slot B (standby) | `0x1C4000` | erased `0xFF` unless an OTA has run |

**The `FW` partition entry is an A/B (OTA) pair, not a single address.**
Entry layout in `partition.bin` at offset `0x34`: `type`, `device`,
`activeIndex`, `name[8]`, then `addr[0]`, `addr[1]`, `maxlen[0]`,
`maxlen[1]` as little-endian `uint32`s:

```text
00000034: 00000046 57000000 00000000 00000100
00000044: 00401c00 00401b00 00401b00 00000000
```

- `activeIndex = 0` -> slot A at `addr[0] = 0x00010000` is what runs
- `addr[1] = 0x001C4000` is the standby slot, `maxlen` `0x1B4000` each

Reading `0x1C4000` and finding all `0xFF` therefore means "no OTA image
staged", **not** "firmware missing". Mistaking `addr[1]` for the firmware
address makes every image comparison fail against blank flash.

To identify which image a board is running, compare at `0x10000`:

```bash
sdk=External/x-cube-st67w61/Projects/ST67W6X_Scripts/Binaries/NCP_Binaries
for image in "$sdk"/st67w611m_{mission,mfg}_*.bin; do
  size=$(wc -c < "$image")
  dd if=dump.bin bs=4096 skip=16 2>/dev/null | head -c "$size" |
    cmp -s - "$image" && echo "MATCH: $(basename "$image")"
done
```

Do **not** try to identify an image by its first 16 bytes: every packaged
image starts with the same `42464e50...` (`BFNP`) container header, so that
signature matches all nine of them. Searching `xxd -p` output with
`grep -bo` is also wrong - it reports offsets into the hex *text*, i.e.
double the real byte offset. Match on a long chunk taken from well inside
the image instead, and confirm the candidate offsets are self-consistent
(a constant delta across several samples) before trusting them.

## Known critical bug (fixed 2026-08-17): write-direction ring overflow

The first real `Program-ST67.sh --force` attempt erased the chip, then the
boot2 write got zero real acks for ~14 minutes straight (every single
write-chunk and the write-check retried on a fixed timeout, never a genuine
ack). Root cause: `USB_TO_UART_RING_SIZE` was 512 bytes, but QConn's own
loader config specifies `tx_size = 2056` - each write chunk arrives over USB
as one burst about 4x the ring's capacity, and `CDC_Receive_FS` always
re-arms the OUT endpoint regardless of ring space (no backpressure), so most
of every write chunk was silently dropped (would show up as a large
`usb_to_uart_overflow_count` on a debugger). The ST67 then received
corrupted/truncated data and never acked it. Reads were unaffected because
the read-direction ring was already 8192 bytes and isn't host-burst-driven.

Fixed by raising `USB_TO_UART_RING_SIZE` to 8192 in `bridge_config.h`
(matches the read-side ring). **This requires reflashing the STM32 with a
rebuilt Bootloader-mode firmware** - it's a firmware bug, not a host-script
bug, so retrying with only the host-tooling fixes applied would not help.

Also confirmed: a failed/interrupted write does not brick the board. The
ST67 ROM bootloader lives in mask ROM, independent of external SPI flash
content, so the same strap/reset entry sequence is always available for a
retry regardless of flash state.

## Real-hardware validation on record

Recorded 2026-08-16, board attached on `COM4`:

- Non-destructive chip/efuse query (`Query-ST67.sh`) succeeded repeatedly:
  chip ID `0052573237531bd700`, default MAC `40:82:7B:03:B5:2C`, part number
  `C6AFDBD111400004`, manufactured 2025 week 16, JTAG disabled, public key
  filled, anti-rollback enabled.
- Actual flash chip: GigaDevice, `jedec_id c46016`, 4 MiB capacity - **not**
  the vendor default `ef4016` (Winbond) assumed by the stock loader config.
- `QConn_Flash_Cmd.exe --read` pulled 1 MiB of real flash content twice,
  independently, byte-for-byte identical (`md5sum` match), with genuine
  byte-value diversity (confirmed non-blank/non-erased real code), over
  ~9 seconds of continuous transfer each time. This is strong evidence the
  bridge is lossless at 2,000,000 baud under sustained real-world load in
  both directions (small commands host->ST67, bulk data ST67->host).

First real `--force` attempt (2026-08-17, before the ring-size fix above)
erased the chip and failed to write boot2.

Recorded 2026-08-18, with the ring-size fix reflashed - **plan Stage F is
closed**:

- `Program-ST67.sh --port COM4 --profile MissionT02 --force` completed a
  full erase/program/verify cycle successfully.
- An independent 4 MiB read-back confirmed it byte-for-byte: boot2, the
  partition table, and the active FW slot at `0x10000` all match their
  vendor files exactly. Active image
  `st67w611m_mission_t02_v2.0.106.bin`, SHA-256
  `7762646fdff5f658659389849f20be0235ce3ba1a917b42e6568318762154a90`.
- That read-back is also the strongest transport evidence on record: a
  1.27 MiB vendor binary recovered with an exact hash match, so the bridge
  is lossless in bulk in both directions.

Recorded 2026-08-19 - **plan Stage D is closed**:

- Debugger-instrumented full 4 MiB read logged `usb_tx_failure_count = 1131`
  and `uart_framing_error_count = 1`; every other counter (both ring
  overflow counters and the remaining three UART hardware error counters)
  read 0.
- Re-hashing that exact dump (not a separate run) still matched boot2, the
  partition table, and the active FW slot byte-for-byte against the vendor
  files. This confirms `usb_tx_failure_count` is a benign retry counter (see
  the note under Reset/session-boundary handling above) and that the single
  framing error was a one-off line glitch that didn't desync either ring or
  corrupt any delivered byte.


## Open items / suggested next steps

1. Re-validate `Dump-ST67-Flash.sh` across boards with different flash chips.
2. Update `ST67_Bypass_Bootloader_Project_Guide.md`: replace the "bootloader
   straps not yet specified" section with the verified sequence in
   `st67_mode.c`, and reconcile its STM32G0B1 references with the actual
   STM32G0B0 target.
3. Delete any stale `build/Debug` / `build/Release` directories left over
   from before the four-preset scheme, if they reappear.
