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

- Use the board's own **USB Serial Device** port, not the ST-LINK virtual COM
  port, which carries no firmware output. The baud rate is ignored. It is
  normally `COM4` on the development workstation, but the number can change
  (see [The COM number changes](#the-com-number-changes)), so let
  `astro_console.py` detect the port rather than passing `--port`.
- `python tools/astro_console.py send status` checks that the board is alive;
  `capture` and `shell` cover longer sessions.
- Only one application can hold the port at a time. Close Serial Monitor, HTerm
  or `astro_console.py` before flashing or opening the port elsewhere.

### First-time setup

The firmware has no built-in WiFi credentials. They default to empty, are set
from the console with `wifi set`, stored in the settings EEPROM, and read on
every connect. See [WiFi.md](WiFi.md).

The API host, path and device key are set from the console with `api host`,
`api path` and `api key` and saved in the EEPROM. Their built-in fallbacks are compile-time values:
`Appli/App/app_config.h` includes `Appli/App/app_credentials.h` when it exists
and takes `APP_ST67_HTTP_HOST`, `APP_ST67_HTTP_PATH` and `APP_ST67_HTTP_KEY`
from it. The file is
git-ignored, so on a fresh checkout copy `Appli/App/app_credentials.h.template`
to `app_credentials.h` and fill in the host and path; without it a board works
only once `api host` and `api path` are set. The API wants the path
`/device/astro/wroclaw` and the stage's `DeviceApiKey` secret (see
`sst/docs/development.md`), which the fetch appends as `?key=<key>`. Prefer
`api key <key>` on the console over `APP_ST67_HTTP_KEY`, so the key lives only
in the board's EEPROM; `api show` and the fetch log show it only as `<set>`. The `APP_ST67_WIFI_SSID` and `APP_ST67_WIFI_PASSWORD` defines in
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

Until 2026-10-05 the STM32 USB CDC port intermittently stopped working, most
often after a flash or a reset. The cause was in the firmware, the USB
interrupt's priority, and is fixed; see [Why it happened](#why-it-happened).
What follows describes the symptoms and the recovery steps as they were, in
case a port is left in a failed state or the problem returns. Typical symptoms:

- `python tools/astro_console.py list` still lists the port, but opening it
  fails with `could not open port 'COM4': FileNotFoundError(2, ...)`.
- Opening it fails with `PermissionError(13, 'A device attached to the system
  is not functioning.', None, 31)`. Despite the exception type, this is Windows
  error 31, a failed device, not another program holding the port.
- The port comes back under a different number, such as `COM7`; see
  [The COM number changes](#the-com-number-changes).
- The port appears in Device Manager but is missing from
  `HKLM\HARDWARE\DEVICEMAP\SERIALCOMM`, which is the authoritative list of
  active serial devices.
- The device reports `CM_PROB_FAILED_START`.

These are device failures. A port held by another application fails instead
with `PermissionError(13, 'Access is denied.', None, 5)`; for that, close the
other holder.

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

#### If the tick has stopped: find where the firmware is

A tick that does not advance means a handler is spinning with interrupts off:
`Error_Handler`, `HardFault_Handler`, `vApplicationStackOverflowHook` or
FreeRTOS's `configASSERT`, all of which end in `while (1)`. The core
registers say which, without reflashing anything:

```bash
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -coreReg
arm-none-eabi-addr2line -e build/Debug/HostControllerA.elf -f -a <PC> <LR>
```

- `PRIMASK = 0x01` confirms the interrupts-off spin. `XPSR` bits 0-8 are the
  active exception: 0 is thread mode (an assert or hook called from a task),
  3 is a HardFault. `CONTROL = 0x02` means the task stack (`PSP`) is in use.
- `addr2line` on `PC` names the loop; `LR` names the caller. On 2026-10-04 this
  gave `vQueueDelete` ← `W6X_Net_DeInit` (a `configASSERT` on a null
  semaphore inside the driver) and, another time, `vApplicationStackOverflowHook`.
- The overflow hook stores the task name: resolve `g_stackOverflowTaskName`
  with `arm-none-eabi-nm`, read the pointer with `-r32`, then the string with
  `-r8 <pointer> 0x10`.
- The log lines queued but never sent are still in `LogService`'s RAM:
  `arm-none-eabi-nm -C` gives the address and size of
  `LogService::instance()::service`; dump it with `-r8` and look for text.

Converting the tick to time: `xTickCount` counts milliseconds, so `0x3AC9C`
is 240 796 ms, 0:04:00.8 of uptime, which places the stop against the console
timestamps.

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

5. **Remove the device node** and let Windows create it again, from an
   *elevated* prompt:

   ```powershell
   pnputil /remove-device "USB\VID_0483&PID_5740\3257327B3534"
   pnputil /scan-devices
   ```

   On 2026-09-30 this was the only step that worked. Once COM4's node was in
   `CM_PROB_FAILED_START`, resets, reflashing, re-plugging and moving to
   another port all came back failed. The instance ID is the board's; list the
   nodes as shown under [The COM number changes](#the-com-number-changes).

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

**Observed on 2026-10-04, over about twenty sessions:** on the development PC
the port opens exactly **once per board reset**. The first `Open()` after a
reset works for as long as it is held; after it is closed, the next `Open()`
fails with Windows error 31 ("A device attached to the system is not
functioning"), `SERIALCOMM` keeps stale duplicates (`USBSER001` and
`USBSER002` both `COM4`), and after a few such cycles the node goes to
`CM_PROB_FAILED_START` or re-enumerates as a new instance (`5&...&0&8`, a new
COM number) in the same state. A software reset (`-rst`) brought the port
back about two times in three, a hardware reset (`mode=HOTPLUG -hardRst`) the
rest of the time, each after waiting 10 to 15 s for enumeration; when neither
did, the elevated `pnputil /remove-device` + `/scan-devices` step was needed.
The firmware was alive every time (tick advancing). The practical rule for
bench work is therefore one session per reset that contains everything the
test needs; `tools/console_capture.ps1` is written for that (open once, send
a scheduled list of commands, log for a fixed time, close).

#### The COM number changes

Windows names a USB device node after the device's serial number when it
accepts one, and otherwise after the hub port it is plugged into. The firmware
reports a serial number built from the chip's unique ID (`Get_SerialNum()` in
`USB_Device/App/usbd_desc.c`), and the node named after it is the one that
holds `COM4`. When an enumeration goes wrong, Windows sometimes creates a
port-based node instead, and that node gets its own COM number. On 2026-09-30
the board came back as `COM7` twice, and later as `COM4` again. The workstation
had accumulated these nodes:

| Instance ID | Port | Kind |
| --- | --- | --- |
| `USB\VID_0483&PID_5740\3257327B3534` | `COM4` | named after the serial number |
| `USB\VID_0483&PID_5740\6&1ECD3977&1&2`, `...&1&3`, `...&1&4` | `COM7`, `COM6`, `COM5` | named after the hub port |
| `USB\VID_0000&PID_0002\...` | none | failed enumerations, `Device Descriptor Request Failed` |

To list them all, including the ones not currently connected (`CM_PROB_PHANTOM`):

```powershell
Get-PnpDevice | Where-Object { $_.InstanceId -like '*VID_0483&PID_5740*' -or
                               $_.InstanceId -like '*VID_0000&PID_0002*' } |
    Select-Object Status, Problem, FriendlyName, InstanceId
```

The number itself does no harm: `astro_console.py` without `--port` takes
whichever USB Serial Device is present. Stale nodes can be removed from an
elevated prompt with `pnputil /remove-device "<instance ID>"`, which frees their
COM numbers. Windows can also be told to ignore the serial number for this
VID/PID (`IgnoreHWSerNum04835740` under
`HKLM\SYSTEM\CurrentControlSet\Control\UsbFlags`), which pins the COM number
to the hub port instead. Neither has been needed so far.

#### Why it happened

**Cause, found and fixed on 2026-10-05: the USB interrupt was serviced too
late.** While an EP0 receive interrupt (`CTR_RX`) is still unserviced, the
STM32 USB peripheral does not answer a new SETUP packet: it stays silent, as
if the packet had been corrupted, so that the pending notification is not
lost (reference manual, control transfers). The host retries a SETUP three
times in quick succession and then fails the request. The USB interrupt had
priority 3, like every other interrupt, so it queued behind the display
refresh interrupt (about 170 us, 1000 times a second), and a USB interrupt
itself takes 100-290 us in the Debug build. When Windows sent a request right
after the previous one's status stage, the board sometimes missed all three
tries.

What Windows did next depended on which request it was:

- **A CDC request while opening the port** (`SET_LINE_CODING`,
  `SET_CONTROL_LINE_STATE`): `SetCommState` failed and the open returned
  error 31, "A device attached to the system is not functioning", with the
  device node still healthy. The next open usually worked.
- **A request during enumeration** (`SET_CONFIGURATION`, a string
  descriptor): the driver failed to start (`CM_PROB_FAILED_START`, Code 10),
  or Windows gave up on the serial number and created a node named after the
  hub port, with a new COM number. This is what the 2026-09-30 trace showed
  ("Windows then stopped: no `SET_CONFIGURATION`") and was read then as a
  Windows driver fault.

Measured with a scripted loop (open, read 0.3 s, close, 0.3 s pause) and a
RAM trace of every control request, read over SWD: at priority 3, 4 of 64 and
11 of 200 opens failed, each time with the board having answered everything
it saw and the failing request absent from its trace, no bus errors
(`ISTR.ERR` never set). With the USB interrupt at priority 1
(`NVIC.USB_UCPD1_2_IRQn` in the `.ioc`, `HAL_PCD_MspInit()` in
`usbd_conf.c`): 0 of 300 and 0 of 200 opens failed, and 10 resets in a row
all re-enumerated under the serial number as `COM4` and opened within 1.5 s.

The display refresh pays a little: under that 200-open storm it logged 24
late shifts and 22 late interrupts (`status`, `display` line) against 11 and
16 at priority 3, and its longest interrupt read 940 us because USB
interrupts now nest inside it. A minute of ordinary console use and an astro
refresh added none.

The suspicion of the September 2026 Windows update (KB5124008) is withdrawn:
it was never confirmed, and the failures stopped with this change on the same
PC and driver (`usbser.sys` 10.0.26100.9278). Device nodes Windows created
during the bad period (`COM5`-`COM9`) remain until removed with `pnputil`.

Checked on the way and harmless: `usbd_conf.h` has `USBD_LPM_ENABLED 1`
(CubeMX's default) while the hardware side has `lpm_enable = DISABLE` and
`usbd_desc.c` provides no BOS descriptor. The library then answers a BOS
request with a stall, which is right for a device whose descriptor says USB
2.00, and Windows does not ask for one. Link power management itself is off
and not wanted on this board.

**Fixed: the USB clock was out of tolerance.** USB runs from HSI48, which
full-speed USB needs within 0.25 %. The firmware did not enable the clock
recovery system (CRS), so HSI48 ran on its factory trim. Once the CRS was
enabled, it settled at `TRIM` 58 instead of the default 64, which puts the
untrimmed clock about 0.8 % fast. The CRS is now enabled in CubeMX (RCC, CRS
SYNC source USB), which generates its setup in `SystemClock_Config()`,
synchronised to the host's start-of-frame packets. This alone did not stop the
failures, which had the cause above.

The CRS should start from `TRIM` 64: the G0's field is 7 bits and 64 is its
reset value and midpoint (`RCC_CRS_HSI48CALIBRATION_DEFAULT`). CubeMX shows
64 as the HSI48 calibration value, but does not save it in the `.ioc` and
generates `HSI48CalibrationValue = 32`, the midpoint on families with a 6-bit
field (seen with the CubeMX in use on 2026-09-30; changing the value and back
did not help). That would start every boot several percent off. `main()` puts
`TRIM` back to 64 in its `SysInit` user code section, before USB starts.

To check the CRS on a running board, read `CRS_CR` over SWD:

```bash
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -r32 0x40006C00 4
```

`0x00003A60` means counting and auto-trim are on (bits 5 and 6) with `TRIM`
at `0x3A`; `0x00004000` means the CRS is off.

**Explained by the cause above:** two early failures in which Windows
abandoned the serial-number string read (and named the node after the hub
port instead, see above) and one descriptor request never completed.

**Ruled out:**

- **The hub.** It fails the same way on a root port of the PC.
- **A reset too short for the host to see a disconnect.** Holding the
  device detached for 100 ms, and later 3 s, after reset before
  `USBD_Start()` did not help.
- **Start-up activity.** Starting USB 3 s after boot, after the WiFi module
  and display are running, did not help.
- **The display pass table.** The previous table fails the same way.

Other ways reported for STM32 CDC devices, not tried here:

- **Flash with the core held in reset** (`STM32_Programmer_CLI -c port=SWD
  mode=UR ...`), so the device looks unplugged during the flash instead of
  attached and silent. It did not bring back a node that had already failed.
- **Use a separate USB-to-UART adapter** for the console. It stays connected
  while the MCU resets. This needs a spare UART on the board.
- **Make the client reconnect** when the port vanishes and comes back.

Sources: [STM32 USB device enumeration (Stm32World
Wiki)](https://stm32world.com/wiki/STM32_USB_Device_Enumeration_(re-enumeration)),
[USB device: re-connect after a reset (ST
Community)](https://community.st.com/t5/stm32-mcus-products/usb-device-re-connect-after-a-reset/td-p/555119),
[USB CDC: how to avoid Windows creating a new COM port for every device (ST
Community)](https://community.st.com/t5/stm32-mcus-embedded-software/usb-cdc-how-to-avoid-windows-from-creating-a-new-com-port-for/td-p/357198),
[USB device-specific registry settings
(Microsoft)](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-device-specific-registry-settings),
[Code 10 - CM_PROB_FAILED_START
(Microsoft)](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/cm-prob-failed-start),
[Windows 11 24H2 known issues
(Microsoft)](https://learn.microsoft.com/en-us/windows/release-health/status-windows-11-24h2),
[CDC-NCM Code 10 after KB5124008 (VirtualBox issue
877)](https://github.com/VirtualBox/virtualbox/issues/877),
[STM32G431 crystal-less USB (ST
Community)](https://community.st.com/t5/stm32-mcus-embedded-software/stm32g431-crystal-less-usb/m-p/313308).

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
