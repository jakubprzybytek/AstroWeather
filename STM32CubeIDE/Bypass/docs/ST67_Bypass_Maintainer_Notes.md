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
`usb_tx_stale_flush_count`, `st67_session_reset_count`. All should read 0
after a healthy session; check them with a debugger after any transport
stress test.

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
- `Dump-ST67-Flash.sh` - bulk flash read-back for manual inspection. **Known
  issue:** it calls `QConn_Eflash.exe -r --flash`, which enforces a strict
  flash-type match against the config's `flash_id` (default `ef4016`,
  Winbond) and hard-fails with `QCC74X FLASH MATCH TYPE FAIL` on hardware
  whose actual flash chip doesn't match (see validation notes below).
  `QConn_Flash_Cmd.exe --read` does **not** have this restriction - it logs
  "flash config Not found, use default" and proceeds. If this script needs
  to work reliably across boards, switch it to `QConn_Flash_Cmd.exe --read`
  instead of `QConn_Eflash.exe --flash -r`.
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
erased the chip and failed to write boot2. Not yet done: a successful full
erase/program/verify cycle with the fixed firmware reflashed (plan Stage F),
and inspecting the diagnostic counters via debugger immediately after to
close out Stage D formally.

## Open items / suggested next steps

1. Reflash the board with a rebuilt `*-Bootloader` firmware (ring-size fix
   above), then retry `Program-ST67.sh --force` against a known-compatible
   profile and record the result (plan Stage F).
2. Inspect the `bridge.c` diagnostic counters via debugger after a large
   transfer to confirm they're all zero (currently only inferred indirectly
   from identical repeated reads).
3. Fix or replace `Dump-ST67-Flash.sh`'s use of `QConn_Eflash.exe --flash -r`
   per the gotcha above.
4. Update `ST67_Bypass_Bootloader_Project_Guide.md`: replace the "bootloader
   straps not yet specified" section with the verified sequence in
   `st67_mode.c`, and reconcile its STM32G0B1 references with the actual
   STM32G0B0 target.
5. Delete any stale `build/Debug` / `build/Release` directories left over
   from before the four-preset scheme, if they reappear.
