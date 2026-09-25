# Development Workflow

This document is the practical build, flash, debug, and device-communication guide for HostControllerA. It is written so that a developer or AI agent can build the firmware, program the board, reach the USB console, and collect evidence that a change works. The console itself, its output format and every command are described in [Console.md](Console.md).

## Prerequisites

Run commands from the repository root:

```text
D:/Workspace/AstroWeather/firmware/HostControllerA
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
"$CUBE_CMAKE" --preset Debug
"$CUBE_CMAKE" --build --preset Debug
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

The CMake presets select the build type. Configure and build the desired preset:

```bash
# Debug
cmake --preset Debug
cmake --build --preset Debug

# Release
cmake --preset Release
cmake --build --preset Release
```

The primary firmware artifact is an ELF file:

```text
build/Debug/HostControllerA.elf
build/Release/HostControllerA.elf
```

For a quick post-build check:

```bash
test -f build/Debug/HostControllerA.elf
arm-none-eabi-size build/Debug/HostControllerA.elf
```

A successful build should leave the ELF present and print the flash/RAM usage summary. The linker script is `STM32G0B1xx_FLASH.ld` and the firmware target is an STM32G0B1 Cortex-M0+ image.

### Shared code and the DisplayController

The remote display boards run the separate `../DisplayController` project: an
STM32G070 with its own CubeMX configuration, the same `Debug` and `Release`
presets, and `build/Debug/DisplayController.elf` as its artifact (see its
[README](../../DisplayController/README.md)). Code used by both images lives in
`../Common` and is compiled into each project, so a change there should be
built in both; see [Architecture.md](Architecture.md#shared-code).

### Native tests

Host-side logic is covered by native tests that do not need a board. The
`NativeTests` preset builds them with the MSYS2 UCRT64 compiler in
`C:/Progs/msys64/ucrt64` (see `CMakePresets.json`) into
`build/native-tests-local`:

```bash
cmake --preset NativeTests
cmake --build --preset NativeTests
ctest --test-dir build/native-tests-local --output-on-failure
```

All suites registered in `tests/CMakeLists.txt` should pass. The shared code
has its own suites in `../Common`, run with the same three commands from that
directory. The list of suites, the coverage preset, the HAL/RTOS stubs and how
to add a test are in [Testing.md](Testing.md).

If `ctest` is unavailable, run an executable directly, for example:

```bash
./build/native-tests-local/tests/settings_codec_tests.exe
```

## Flash and Start Debugging

The repository has a VS Code launch configuration named **HostController Debug** in `.vscode/launch.json`. It uses the `stlinkgdbtarget` adapter, runs the STM32 debug-launch pre-build command, and programs/debugs:

```text
build/Debug/HostControllerA.elf
```

To build, flash, and start a debug session:

1. Connect the ST-LINK probe and power the board.
2. Open the repository root in VS Code.
3. Select **HostController Debug** in Run and Debug.
4. Press `F5`.
5. Wait for the ST-LINK connection and program-download messages in the Debug Console.

The F5 launch is the preferred flashing method for this repository because it uses the configured ST-LINK debug adapter and the correct ELF/symbol file. It also rebuilds through the configured `preBuild` command.

The launch file provides F5 entries for HostController Debug and HostController Release. The DisplayController project has its own `.vscode/launch.json` with the same two entries for `build/Debug/DisplayController.elf` and `build/Release/DisplayController.elf`.

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
STM32_Programmer_CLI -c port=SWD -w build/Debug/HostControllerA.elf -v -rst
```

The CLI is installed on the current workstation at:

```text
C:/Program Files/ST/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI
```

Do not substitute a serial COM port for `port=SWD`; flashing uses the ST-LINK
SWD connection.

## Communicate with the Device

The HostController firmware exposes a USB CDC virtual COM port carrying the log
and the command console. [Console.md](Console.md) covers connecting, the
`tools/astro_console.py` client, the output format and the full command
reference. In short:

- Use the board's own **USB Serial Device** port (`COM4` on the development
  workstation), not the ST-LINK virtual COM port, which carries no firmware
  output. The baud rate is ignored.
- `python tools/astro_console.py send status` checks that the board is alive;
  `capture` and `shell` cover longer sessions.
- Only one application can hold the port at a time. Close Serial Monitor, HTerm
  or `astro_console.py` before flashing or opening the port elsewhere.

### First-time setup

The firmware has no built-in WiFi credentials. They default to empty, are set
from the console with `wifi set`, stored in the settings EEPROM, and read on
every connect. See [WiFi.md](WiFi.md).

The API host and path are set from the console with `api host` and `api path`
and saved in the EEPROM. Their built-in fallbacks are compile-time values:
`Appli/App/app_config.h` includes `Appli/App/app_credentials.h` when it exists
and takes `APP_ST67_HTTP_HOST` and `APP_ST67_HTTP_PATH` from it. The file is
git-ignored, so on a fresh checkout copy `Appli/App/app_credentials.h.template`
to `app_credentials.h` and fill in the host and path; without it a board works
only once `api host` and `api path` are set. The `APP_ST67_WIFI_SSID` and `APP_ST67_WIFI_PASSWORD` defines in
the same file are no longer used by the firmware.

After flashing a new board:

```text
wifi set MyNetwork mypassphrase
time trim <ppm>
status
```

`wifi set` saves and immediately runs a connection test. `time trim` applies
this board's measured LSI error; see [RTC.md](RTC.md#trimming). Both are kept
in the EEPROM across power cycles.

### COM port disappears or will not open

The STM32 USB CDC port intermittently stops working, most often after repeated
flash and reset cycles. This is a known issue with the ST USB device stack
rather than a fault in this firmware. Typical symptoms:

- `python tools/astro_console.py list` still lists the port, but opening it
  fails with `could not open port 'COM4': FileNotFoundError(2, ...)`.
- The port appears in Device Manager but is missing from
  `HKLM\HARDWARE\DEVICEMAP\SERIALCOMM`, which is the authoritative list of
  active serial devices.
- The device reports `CM_PROB_FAILED_START`.

The open failure is `FileNotFoundError` (the device is gone), not
`PermissionError` (another application holds the port). For the latter, close
the other holder instead.

Reading the RTC over SWD (`python tools/rtc_offset.py`) is another way to see
that the firmware is alive while the console is unreachable.

#### First, confirm the firmware is still running

Do this before power cycling anything. A lost port and a hung or crash-looping
firmware look identical from the host, and the remedies are different. Read the
FreeRTOS tick counter twice over SWD: if it advances, the scheduler is alive and
the problem is on the Windows side.

```bash
# The address changes between builds, so resolve it from the ELF.
arm-none-eabi-nm build/Debug/HostControllerA.elf | grep " xTickCount"

STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -r32 <address> 0x4
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -r32 <address> 0x4
```

`mode=HOTPLUG` attaches without resetting, so the running firmware is left
undisturbed. A value that increases between the two reads shows the scheduler is
running. A value far larger than a few seconds also rules out a reset loop,
since the counter would otherwise keep restarting from zero.

#### Recover the port

In order of escalation:

1. **Hardware reset.** Usually sufficient, and the quickest option:

   ```bash
   STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -hardRst
   ```

   A software reset (`-rst`) often is *not* enough. It does not always drop the
   USB connection long enough for Windows to tear the device down and
   re-enumerate it.

2. **Unplug and reconnect the USB cable.** Forces a full re-enumeration.

3. **Restart the device node**, from an *elevated* PowerShell:

   ```powershell
   $id = (Get-PnpDevice -PresentOnly |
          Where-Object { $_.InstanceId -like '*VID_0483&PID_5740*' }).InstanceId
   Disable-PnpDevice -InstanceId $id -Confirm:$false
   Enable-PnpDevice  -InstanceId $id -Confirm:$false
   ```

   Without elevation these fail with `Generic failure`. When enumeration has
   already failed, the board appears not as `VID_0483&PID_5740` but as an
   `Unknown USB Device (Device Descriptor Request Failed)` with
   `VID_0000&PID_0002`, so that instance has to be restarted instead.

4. **Move the cable to a different USB port on the PC.** Seen once on
   2026-09-22 to be the only thing that worked, after the three steps above had
   all failed and the device sat in `CM_PROB_FAILED_POST_START`. This
   re-enumerates the board on another host controller. It also cuts power, so
   the RTC loses the time; see [RTC.md](RTC.md#reset-and-power-loss).

Confirm recovery with:

```powershell
Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like '*VID_0483&PID_5740*' } |
    Select-Object Status, Problem, FriendlyName
```

`Status: OK` and `Problem: CM_PROB_NONE` mean the port is usable again. The port
should also reappear under `SERIALCOMM`:

```powershell
Get-ItemProperty 'HKLM:\HARDWARE\DEVICEMAP\SERIALCOMM'
```

#### Reduce how often it happens

- Wait a few seconds after a flash or reset before reopening the port.
  Enumeration is not instant, and opening during that window is what most often
  leaves the device node in the failed state.
- Close `astro_console.py`, Serial Monitor, or any other holder of the port
  before flashing.
- Prefer `-hardRst` over `-rst` when a reset is needed as part of flashing.

## Validation Checklist

Use the narrowest checks appropriate to the change:

1. Confirm the relevant source change is in `User/` or an intended user-code section.
2. Configure and build the affected preset.
3. Confirm the expected ELF exists and run `arm-none-eabi-size` on it.
4. Stop any old debug session, then press F5 with **HostController Debug** selected.
5. Confirm the Debug Console reports a successful ST-LINK connection and program download.
6. Wait a few seconds for USB to enumerate, then open the console and check the
   welcome line shows the new build time, e.g.
   `python tools/astro_console.py capture --duration 10`.
7. Send `status` (`python tools/astro_console.py send status`) and check the
   `OK status` reply.
8. Send the command relevant to the change and retain the output.
9. For communication or timing changes, run `stats on` and leave the capture
   running long enough to see several five-second reports, the `dropped` and
   `busyDrop` counters and any warnings or errors.
10. Stop the debug session before disconnecting the probe or reopening the COM port in another tool.

For a change that affects only host-side logic, run the native test suite as
well. For a firmware behavior change, the build plus a programmed-board console
capture is the minimum meaningful validation.

If a WiFi fetch fails with `[ERR] sem_if_ready not received`, the ST67 module
did not signal ready when the driver started. That is a module or SPI transport
problem, separate from the console path; see [WiFi.md](WiFi.md).
