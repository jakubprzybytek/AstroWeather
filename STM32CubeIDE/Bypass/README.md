# AstroWeather ST67 Bypass

## Purpose

Firmware for an STM32G0B0-based board that turns a PC's USB connection into a
binary-transparent bridge to an ST67W611M Wi-Fi/BLE NCP module:

```text
PC (USB CDC)  <->  STM32G0B0 Bypass firmware  <->  USART2 (2,000,000 baud)  <->  ST67W611M
```

It supports two purposes:

1. **UART bypass ("Manufacture" mode)** - a transparent pass-through so a PC
   tool can talk to the ST67's normal UART shell/AT-style interface.
2. **Bootloader mode** - applies the ST67's verified bootloader strap/reset
   sequence and then provides the same transparent transport, so the PC-side
   vendor tool `QConn_Flash_Cmd` can erase/program/verify the ST67 over USB
   instead of needing direct wiring to the module's UART pins.

The STM32 firmware never parses or interprets the ST67 protocol in either
mode; it only applies the correct GPIO strap sequence and moves bytes.

See [docs/ST67_Bootloader_Mode_Implementation_Plan.md](docs/ST67_Bootloader_Mode_Implementation_Plan.md)
for the full design rationale and validation plan, and
[docs/ST67_Bypass_Maintainer_Notes.md](docs/ST67_Bypass_Maintainer_Notes.md)
for architecture details, known tooling quirks, and validation history useful
for future maintenance.

## Building

Prerequisites: `arm-none-eabi-gcc`, `cmake`, and `ninja`. If you have
STM32CubeIDE installed instead of a standalone toolchain, it bundles all
three under its `plugins/com.st.stm32cube.ide.mcu.externaltools.*/tools/bin`
folders - add those to `PATH` for the commands below.

The project builds with four CMake presets, one per combination of build
type and ST67 startup mode (`CMakePresets.json`):

| Preset | Build type | ST67 startup mode |
|---|---|---|
| `Debug-Manufacture` | Debug | Manufacture (default) |
| `Release-Manufacture` | Release | Manufacture (default) |
| `Debug-Bootloader` | Debug | Bootloader |
| `Release-Bootloader` | Release | Bootloader |

```bash
cmake --preset Debug-Manufacture
cmake --build --preset Debug-Manufacture
```

Each preset configures into its own `build/<PresetName>` directory and
produces a `Bypass.elf` there, so manufacture and bootloader builds never
overwrite each other. The startup mode is also a plain CMake cache variable
if you prefer configuring manually:

```bash
cmake -S . -B build/manual -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake \
  -DCMAKE_BUILD_TYPE=Debug -DST67_DEFAULT_MODE=BOOTLOADER
```

Flash the resulting `Bypass.elf` with your usual SWD tool (STM32CubeProgrammer,
STM32CubeIDE debug launch, OpenOCD, etc.) - this project does not script
flashing the STM32 itself.

## Usage

### UART bypass (Manufacture mode)

1. Flash a `*-Manufacture` build.
2. Plug the board in over USB. It enumerates as a CDC serial port named
   **"AstroWeather ST67 Bypass"**.
3. Open that port with any terminal/tool. Opening it (DTR asserted) triggers
   an ST67 reset into Manufacture mode so its startup banner/output isn't
   missed. Bytes sent/received over the port are forwarded to/from the ST67's
   UART untouched - no line parsing, no echo, no translation.
4. Two status LEDs: **LED_1** lights while data is being sent to the ST67,
   **LED_2** lights while data is being received from the ST67 (both also
   used as a basic "is this thing alive" indicator, since LED_1 stays on
   from boot until the first real transfer).

### Programming the ST67 (Bootloader mode)

1. Flash a `*-Bootloader` build. On mode entry the firmware applies the
   verified `BOOT=1`/`CHIP_EN` reset sequence and keeps `CS` high, putting
   the ST67 into its ROM bootloader instead of its normal shell.
2. Optionally verify communication first without any risk of changing the
   device, using the non-destructive chip/efuse query:

   ```bash
   ./tools/Query-ST67.sh --port COM4
   ```

   This only reads the efuse/identity region; it never needs `--force`.
3. Program a signed vendor image with the Bypass-specific wrapper around
   `QConn_Flash_Cmd`:

   ```bash
   ./tools/Program-ST67.sh --port COM4 --profile MissionT01 --force
   ```

   (`--profile` is one of `MissionT01`, `MissionT02`, `Manufacturing`.)
   Without `--force` the script validates every input (vendor tool, config,
   image, efuse file) and prints a lock-risk warning, then stops before
   touching the device - use that first to sanity-check your arguments.
   A PowerShell equivalent, `tools/Program-ST67.ps1`, is also available with
   the same flags for native PowerShell/CI use on Windows.
4. `tools/Dump-ST67-Flash.sh` can read back flash content for inspection;
   see [docs/ST67_Bypass_Maintainer_Notes.md](docs/ST67_Bypass_Maintainer_Notes.md)
   for a caveat about which underlying vendor tool that script uses.

None of the host scripts ever invoke STM32CubeProgrammer or select a NUCLEO
host image - they only ever touch the Bypass board's own USB CDC port and
ST67 NCP images.
