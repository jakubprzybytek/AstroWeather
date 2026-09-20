# Development Workflow

This document is the practical build, flash, debug, and device-communication guide for HostControllerA. It is written so that a developer or AI agent can build the firmware, program the board, exercise the USB CDC interface, and collect evidence that a change works.

## Prerequisites

Run commands from the repository root:

```text
D:/Workspace/AstroWeather/STM32CubeIDE/HostControllerA
```

The following commands should be available in the Bash environment:

```bash
command -v cmake
command -v ninja
command -v arm-none-eabi-gcc
command -v arm-none-eabi-g++
command -v arm-none-eabi-objcopy
command -v arm-none-eabi-size
```

This project uses the STM32CubeIDE-bundled tools. On the current workstation,
the ARM GNU tools are on `PATH`, but `cmake`, `ninja`, and `ctest` are not.
Use the bundled Cube CMake executable when needed:

```bash
CUBE_CMAKE="/c/Users/jakub/.vscode/extensions/stmicroelectronics.stm32cube-ide-build-cmake-1.46.0-win32-x64/resources/cube-cmake/win32/x86_64/cube-cmake.exe"
"$CUBE_CMAKE" --preset Debug-HostController
"$CUBE_CMAKE" --build --preset Debug-HostController
```

On another installation, add the CMake and Ninja `tools/bin` directories for
the current shell. The exact versioned directory names can differ. For example:

```bash
export PATH="/c/Program Files/ST/STM32CubeIDE_2.0.0/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.cmake.win32_1.1.0.202409170845/tools/bin:$PATH"
export PATH="/c/Program Files/ST/STM32CubeIDE_2.0.0/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.ninja.win32_1.1.0.202511131536/tools/bin:$PATH"
```

The ARM GNU tools must also be on `PATH`. Confirm the toolchain before configuring a firmware build:

```bash
arm-none-eabi-gcc --version
cmake --version
ninja --version
```

For serial validation, install `pyserial` once if needed:

```bash
python -m pip install --quiet pyserial
```

A connected ST-LINK probe and a USB cable are required for flashing and USB CDC communication. The board's USB CDC port and ST-LINK connection may be separate interfaces.

## Build Firmware

The CMake presets select the firmware variant and build type. Configure and build the desired preset:

```bash
# Debug HostController
cmake --preset Debug-HostController
cmake --build --preset Debug-HostController

# Debug DisplayController
cmake --preset Debug-DisplayController
cmake --build --preset Debug-DisplayController

# Release HostController
cmake --preset Release-HostController
cmake --build --preset Release-HostController

# Release DisplayController
cmake --preset Release-DisplayController
cmake --build --preset Release-DisplayController
```

The primary firmware artifact is an ELF file:

```text
build/Debug-HostController/HostControllerA.elf
build/Debug-DisplayController/HostControllerA.elf
build/Release-HostController/HostControllerA.elf
build/Release-DisplayController/HostControllerA.elf
```

For a quick post-build check:

```bash
test -f build/Debug-HostController/HostControllerA.elf
arm-none-eabi-size build/Debug-HostController/HostControllerA.elf
```

A successful build should leave the ELF present and print the flash/RAM usage summary. The linker script is `STM32G0B1xx_FLASH.ld` and the firmware target is an STM32G0B1 Cortex-M0+ image.

### Native tests

Native tests use the `NativeTests` preset and do not require a board:

```bash
cmake --preset NativeTests
cmake --build --preset NativeTests
ctest --test-dir build/native-tests-local --output-on-failure
```

A successful test run should report `astro_data_parser_tests`,
`numeric_display_tests`, `display_codec_tests`, and
`current_sense_conversion_tests` as passing. If `ctest` is unavailable, run
the generated executables directly:

```bash
./build/native-tests-local/tests/astro_data_parser_tests.exe
./build/native-tests-local/tests/numeric_display_tests.exe
./build/native-tests-local/tests/display_codec_tests.exe
./build/native-tests-local/tests/current_sense_conversion_tests.exe
```

## Flash and Start Debugging

The repository has a VS Code launch configuration named **HostController Debug** in `.vscode/launch.json`. It uses the `stlinkgdbtarget` adapter, runs the STM32 debug-launch pre-build command, and programs/debugs:

```text
build/Debug-HostController/HostControllerA.elf
```

To build, flash, and start a debug session:

1. Connect the ST-LINK probe and power the board.
2. Open the repository root in VS Code.
3. Select **HostController Debug** in Run and Debug.
4. Press `F5`.
5. Wait for the ST-LINK connection and program-download messages in the Debug Console.

The F5 launch is the preferred flashing method for this repository because it uses the configured ST-LINK debug adapter and the correct ELF/symbol file. It also rebuilds through the configured `preBuild` command.

The configured launch file currently provides F5 entries for HostController Debug and HostController Release only. DisplayController has build presets but no corresponding launch entry in `.vscode/launch.json`.

### Stop a debug session

Use any of these methods:

- Press `Shift+F5`.
- Click the red square **Stop** button in the Debug toolbar.
- Run **Debug: Stop** from the Command Palette.

Stop the existing session before pressing F5 again. The reliable sequence is
**Debug: Stop**, followed by F5. Starting F5 while an old session is active
can show an `already running` confirmation; do not start a second debug
instance against the same ST-LINK probe.

The VS Code command `workbench.action.debug.start` is the programmatic
equivalent of F5. To restart cleanly, invoke `workbench.action.debug.stop`
first, then `workbench.action.debug.start`.

### Command-line flashing

The project does not define a custom flash target. A standalone STM32CubeProgrammer command would normally be:

```bash
STM32_Programmer_CLI -c port=SWD -w build/Debug-HostController/HostControllerA.elf -v -rst
```

The CLI is installed on the current workstation at:

```text
C:/Program Files/ST/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI
```

Do not substitute a serial COM port for `port=SWD`; flashing uses the ST-LINK
SWD connection.

## Communicate with the Device

The HostController firmware exposes a USB CDC virtual COM port for logs, telemetry, healthcheck echoes, and console commands. The DisplayController variant does not start the HostController debug service.

### Connection settings

- Find the assigned port in Windows Device Manager. In the verified setup,
  `COM4` is the active USB Serial Device carrying the firmware console;
  `COM3` is the ST-LINK virtual COM port and produced no application output.
- Baud rate is ignored by USB CDC; `115200` is a conventional value.
- Send lines terminated with `\n` or `\r\n`.
- Only one application can hold the COM port at a time; see [Console CLI tool](#console-cli-tool).

### Interactive monitor

The `eclipse-cdt.serial-monitor` extension can be used for manual checks:

1. Open the Command Palette.
2. Run **Serial Monitor: Start Monitoring**.
3. Select the board's COM port and any baud rate, such as `115200`.
4. Send `help` followed by Enter.

### Console CLI tool

`tools/astro_console.py` wraps the connect/send/read pattern below into a
reusable CLI, so ad-hoc capture scripts don't need to be rewritten for each
check. It requires `pyserial` (see Prerequisites). Run it from the
`HostControllerA` repository root.

By default it auto-detects the console port by excluding any port whose
description contains `ST-LINK`/`STLink` (the ST-LINK virtual COM port), and
picks the remaining single candidate. Pass `--port COM4` (before the
subcommand) to override auto-detection, or if more than one non-ST-LINK port
is present.

List candidate ports:

```bash
python tools/astro_console.py list
```

Send one or more commands and print the response (waits `--wait` seconds,
default `2`, after each):

```bash
python tools/astro_console.py send help
python tools/astro_console.py send "adc log on" "adc log off"
```

Open an interactive shell: typed lines are sent as commands, device output
streams live, `Ctrl+C` exits:

```bash
python tools/astro_console.py shell
```

Capture device output for a fixed duration, optionally sending a command
partway through and writing the result to a file instead of stdout:

```bash
python tools/astro_console.py capture --duration 20 --command "astro refresh" --output capture.txt
```

Only one application can hold the COM port at a time. Close VS Code Serial
Monitor or any other terminal (e.g. HTerm) before running this tool, and
stop the tool before opening the port elsewhere.

For anything the CLI doesn't cover, the same connect/send/read pattern can be
scripted directly with `pyserial`:

```python
import serial
import time

PORT = "COM4"
BAUD = 115200
DURATION = 15
SEND_AT = 3

with serial.Serial(PORT, BAUD, timeout=0.2) as ser:
    start = time.monotonic()
    sent = False
    data = bytearray()

    while time.monotonic() - start < DURATION:
        elapsed = time.monotonic() - start
        if not sent and elapsed >= SEND_AT:
            ser.write(b"help\r\n")
            ser.flush()
            sent = True
        chunk = ser.read(256)
        if chunk:
            data.extend(chunk)

print(data.decode(errors="replace"))
```

Do not commit throwaway one-off scripts written this way; extend
`tools/astro_console.py` instead if the capability is worth keeping.

### Useful commands

The command set can change; send `help` first. Current commands include:

```text
help
status
display set <index> <value> <precision>
display time <index> <HH:MM>
display blank <index>
astro refresh
adc log on
adc log off
adc display on
adc display off
eeprom probe
eeprom dump
eeprom read <hex-offset> [hex-length]
eeprom write <hex-offset> <hexbytes>
eeprom erase
```

Every completed input line produces an echo similar to:

```text
[0:00:01:23]: received text
```

Logs have this form:

```text
[days:hours:minutes:seconds] [INFO] message
[days:hours:minutes:seconds] [WARN] message
[days:hours:minutes:seconds] [ERR] message
[days:hours:minutes:seconds] [DEBUG] message
```

When the device has been inactive for approximately five seconds, it emits periodic `[STATS]`, `[MEM]`, and `[STACK]` telemetry. These records can be interleaved with command responses.

Verified USB CDC command sequence on `COM4`:

```text
astro refresh
astro refresh
```

The first command returns immediately with:

```text
OK astro-refresh=started
```

The second command, while the first refresh is active, returns:

```text
ERR astro-refresh-busy
```

The refresh then reports asynchronous ST67/network results through the log.
Successful command-path testing was observed; a separate run failed at ST67
initialization with `sem_if_ready not received`, so that transport failure is
distinct from console-trigger validation.

## Validation Checklist

Use the narrowest checks appropriate to the change:

1. Confirm the relevant source change is in `User/` or an intended user-code section.
2. Configure and build the affected preset.
3. Confirm the expected ELF exists and run `arm-none-eabi-size` on it.
4. Stop any old debug session, then press F5 with **HostController Debug** selected.
5. Confirm the Debug Console reports a successful ST-LINK connection and program download.
6. Identify the USB CDC COM port and capture startup output, e.g. `python tools/astro_console.py capture --duration 10`.
7. Send `help` (`python tools/astro_console.py send help`) and verify an echo/command response.
8. Send `status` or the command relevant to the change and retain the output.
9. For communication changes, leave the capture running long enough to observe the five-second telemetry behavior and any warnings/errors.
10. Stop the debug session before disconnecting the probe or reopening the COM port in another tool.

For a change that affects only host-side logic, run the native CTest suite as well. For a firmware behavior change, the build plus a programmed-board serial capture is the minimum meaningful validation.
