# Development

How to build, test, flash and talk to the AstroWeather firmware, and how to
keep application code out of the CubeMX-generated files. It covers every
firmware project:

| Project | Target | Artifact |
| --- | --- | --- |
| [HostControllerA](../HostControllerA/README.md) | STM32G0B1, the Wi-Fi host | `build/Debug/HostControllerA.elf` |
| [DisplayController](../DisplayController/README.md) | STM32G070, a remote display board | `build/Debug/DisplayController.elf` |
| [Common](../Common/README.md) | Code compiled into both; native tests only | none |
| [Bypass](../Bypass/README.md) | STM32G0B0 bench board, a USB-to-UART bridge for programming the ST67 module | see its README |

The console commands are in the host's [Console.md](../HostControllerA/Docs/Console.md).
To set up a new host board (Wi-Fi credentials, server, clock trim), follow
[Quick Start](../HostControllerA/README.md#quick-start) after flashing.

## Prerequisites

Commands below are run from the project directory, for example
`firmware/HostControllerA`, in Git Bash.

| Tool | Where it comes from on the development workstation |
| --- | --- |
| `cmake`, `ninja` | The STM32CubeIDE plugins, on `PATH`: `C:/Program Files/ST/STM32CubeIDE_2.0.0/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.cmake.win32_*/tools/bin` and `...ninja.win32_*/tools/bin` |
| `arm-none-eabi-*` | The STM32Cube bundle, on `PATH`: `C:/Users/<user>/AppData/Local/stm32cube/bundles/gnu-tools-for-stm32/14.3.1+st.2/bin` |
| `STM32_Programmer_CLI` | STM32CubeProgrammer, on `PATH`: `C:/Program Files/ST/STM32Cube/STM32CubeProgrammer/bin` |
| Host C++ compiler for native tests | MSYS2 UCRT64 in `C:/Progs/msys64/ucrt64`, named in each `CMakePresets.json` |
| Python with `pyserial` | `python -m pip install pyserial`, for `tools/astro_console.py` |

The versioned directory names differ between installations. To put the CubeIDE
tools on `PATH` for one shell:

```bash
export PATH="/c/Program Files/ST/STM32CubeIDE_2.0.0/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.cmake.win32_1.1.0.202409170845/tools/bin:$PATH"
export PATH="/c/Program Files/ST/STM32CubeIDE_2.0.0/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.ninja.win32_1.1.0.202511131536/tools/bin:$PATH"
arm-none-eabi-gcc --version && cmake --version && ninja --version
```

The `cube-cmake.exe` wrapper of the VS Code STM32Cube extension does not work
from a plain shell (`'cube' command is not available in current context`); use
the plain `cmake`.

The native tests need the MSYS2 compiler's directory on `PATH` as well:

```bash
export PATH="/c/Progs/msys64/ucrt64/bin:$PATH"
```

Without it every compile fails with `FAILED: [code=1]` and no message (the
compiler cannot load its DLLs), and `ctest` then quietly runs the binaries of
the last good build.

Flashing needs an ST-LINK on the board's SWD header; the console needs the
host board's own USB connector. They are separate connections.

## Build

Both firmware projects have the CMake presets `Debug` and `Release`:

```bash
cmake --preset Debug
cmake --build --preset Debug
arm-none-eabi-size build/Debug/HostControllerA.elf      # or DisplayController.elf
```

The link step prints the use of each memory region. On the host these are
`RAM` (139 KiB), `NOINIT` (the 5 KiB retained error log) and `FLASH`.

Every host build regenerates `BuildInfo.cpp` with the build time
(`cmake/BuildInfo.cmake`), which the console's welcome line and `status`
report. There is no version number or git hash.

### Shared code

`firmware/Common` is not a library. Each project includes
`Common/CommonSources.cmake` and compiles the shared sources itself, against
its own HAL, `main.h` and `FreeRTOSConfig.h`. A change under `Common/` must
therefore be built in both projects. What the shared code may and may not use
is in [Common/README.md](../Common/README.md).

## Native Tests

Logic that needs no hardware is tested natively, with the host compiler, in
every project:

```bash
cmake --preset NativeTests
cmake --build --preset NativeTests
ctest --test-dir build/native-tests-local --output-on-failure
```

Run these from `firmware/HostControllerA`, `firmware/DisplayController` and
`firmware/Common`; each has its own suites. `NativeTests-Coverage` adds
coverage. The suites, the stubs and how to write a test are in
[Testing.md](Testing.md). A suite can also be run on its own, for example
`./build/native-tests-local/tests/settings_codec_tests.exe`.

## Flash and Debug

### From VS Code

Each firmware project has `.vscode/launch.json` with a Debug and a Release
entry (**HostController Debug**, **DisplayController Debug**, ...). They use
the `stlinkgdbtarget` adapter, rebuild through the `preBuild` command, program
the ELF and start a debug session:

1. Connect the ST-LINK and power the board.
2. Select the entry in Run and Debug and press `F5`.
3. Wait for the connection and download messages in the Debug Console.

Stop a session (`Shift+F5`, or **Debug: Stop**) before starting another;
two sessions against one probe fail.

### From the command line

```bash
# HostControllerA
STM32_Programmer_CLI -c port=SWD -w build/Debug/HostControllerA.elf -v -rst
# DisplayController
STM32_Programmer_CLI -c port=SWD -w build/Debug/DisplayController.elf -v -hardRst
```

`port=SWD` is the ST-LINK, never a COM port. `-hardRst` resets through the
reset line and is the surer of the two; a software reset (`-rst`) does not
always drop the host's USB connection long enough for Windows to re-enumerate
it. `mode=HOTPLUG` attaches to a running board without resetting it:

```bash
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -r32 <address> <bytes>
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -hardRst
```

Resolve addresses from the ELF with `arm-none-eabi-nm` (`-C` for C++ names, `-S`
for sizes). The DisplayController has no console yet; its counters are read
this way ([Diagnostics](../DisplayController/Docs/Architecture.md#diagnostics)).

## Connecting over USB CDC

The host board's USB connector is a CDC virtual COM port carrying the log and
the console. How the USB device is set up in the firmware is in
[Console.md](../HostControllerA/Docs/Console.md#usb-device).

### Which port

The board shows up as a **USB Serial Device** (`VID_0483&PID_5740`), normally
`COM4` on the development workstation. The ST-LINK adds a second port, the
*STMicroelectronics STLink Virtual COM Port*, which carries no firmware
output.

- The baud rate is ignored; `115200` is conventional.
- End each line with `\n`; `\r` is ignored, so `\r\n` works too.
- Only one program can hold the port. Close Serial Monitor, HTerm or
  `astro_console.py` before opening it elsewhere and before flashing.
- Wait a few seconds after a flash or reset for enumeration before opening it.

Opening the port prints a welcome line with the build time; see
[Console.md](../HostControllerA/Docs/Console.md#welcome-message).

### VS Code Serial Monitor

With the `eclipse-cdt.serial-monitor` extension: **Serial Monitor: Start
Monitoring**, select the USB Serial Device and any baud rate, set the line
ending to LF or CRLF, type `help`.

### astro_console.py

`tools/astro_console.py` in `firmware/HostControllerA` is the scriptable
client:

```bash
python tools/astro_console.py list                          # ports and descriptions
python tools/astro_console.py send status                   # one command
python tools/astro_console.py send "adc log on" "adc log off"
python tools/astro_console.py shell                         # interactive, Ctrl+C exits
python tools/astro_console.py capture --duration 20 --command "astro refresh" --output capture.txt
```

| Option | Applies to | Default | Meaning |
| --- | --- | --- | --- |
| `--port COM4` | all, before the subcommand | auto | Port to open. Auto-detection skips any port whose description contains `ST-LINK` or `STLink` and takes the one left; it fails if none or several remain. |
| `--baud N` | all, before the subcommand | `115200` | Passed to pyserial; ignored by the device. |
| `--wait S` | `send` | `2` | Seconds to read after each command. |
| `--duration S` | `capture` | `15` | Total capture time. |
| `--command TEXT` | `capture` | none | One command to send during the capture. |
| `--send-at S` | `capture` | `3` | When to send `--command`, in seconds from the start. |
| `--output FILE` | `capture` | stdout | Write the capture to a file. |

Each command is sent with `\r\n`, and every invocation opens the port, so its
output starts with the welcome. `send` reads only for `--wait` seconds, too
short for the asynchronous result of `astro refresh` or `wifi test`; use
`capture` or `shell` for those. Let the tool find the port rather than passing
`--port`, since the COM number can change. When something is not covered,
extend the tool rather than writing a throwaway pyserial script.

Two more tools in `firmware/HostControllerA/tools`:

- `console_capture.ps1` opens the port once, sends a scheduled list of
  commands, logs for a fixed time and closes: one session holding a whole test.
- `rtc_offset.py` reads the RTC over SWD, without the console; see
  [RTC.md](../HostControllerA/Docs/RTC.md#measuring-the-drift).

## COM Port Troubleshooting

### Symptoms

- `astro_console.py list` shows the port, but opening it fails with
  `FileNotFoundError(2, ...)`, or with `PermissionError(13, 'A device attached
  to the system is not functioning.', None, 31)`: Windows error 31, a failed
  device, not another program holding the port.
- The port comes back under another number, such as `COM7`; see
  [The COM number changes](#the-com-number-changes).
- The device node reports `CM_PROB_FAILED_START` (Code 10), or the port is
  missing from `HKLM\HARDWARE\DEVICEMAP\SERIALCOMM`, the list of active serial
  devices.

A port held by another program fails differently, with
`PermissionError(13, 'Access is denied.', None, 5)`; close the other holder.

### Check that the firmware is running

A lost port and a hung firmware look the same from the PC. Read the FreeRTOS
tick twice over SWD:

```bash
arm-none-eabi-nm build/Debug/HostControllerA.elf | grep " xTickCount"
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -r32 <address> 0x4
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -r32 <address> 0x4
```

If the value grows, the scheduler runs and the problem is on the USB or
Windows side. `xTickCount` counts milliseconds, so it also gives the uptime to
compare with the console's timestamps; a value of many seconds rules out a
reset loop.

### If the tick has stopped

A handler is spinning with interrupts off: `Error_Handler`,
`HardFault_Handler`, `vApplicationStackOverflowHook` or a FreeRTOS
`configASSERT`. The core registers say which:

```bash
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -coreReg
arm-none-eabi-addr2line -e build/Debug/HostControllerA.elf -f -a <PC> <LR>
```

- `PRIMASK = 0x01` confirms interrupts are off. `XPSR` bits 0-8 are the
  active exception: 0 is thread mode (an assert or hook called from a task),
  3 a HardFault. `CONTROL = 0x02` means a task stack (`PSP`) is in use.
- `addr2line` on `PC` names the loop and on `LR` its caller.
- The stack overflow hook keeps the task's name: resolve
  `g_stackOverflowTaskName` with `arm-none-eabi-nm`, read the pointer with
  `-r32`, then the string with `-r8 <pointer> 0x10`.
- Log lines queued but never sent are still in RAM: `arm-none-eabi-nm -C`
  gives the address and size of `LogService::instance()::service`; dump it
  with `-r8` and look for text. Warnings and errors are also kept in the
  retained [error log](../HostControllerA/Docs/Console.md#error-log), which
  `errors` lists after the reset.

### Recover the port

In order of escalation:

1. **Hardware reset**, usually enough:
   `STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -hardRst`.
2. **Unplug and reconnect the USB cable.** This also cuts power, so the RTC
   loses the time ([RTC.md](../HostControllerA/Docs/RTC.md#reset-and-power-loss)).
3. **Restart the device node**, from an elevated PowerShell:

   ```powershell
   $id = (Get-PnpDevice -PresentOnly |
          Where-Object { $_.InstanceId -like '*VID_0483&PID_5740*' }).InstanceId
   Disable-PnpDevice -InstanceId $id -Confirm:$false
   Enable-PnpDevice  -InstanceId $id -Confirm:$false
   ```

   Without elevation these fail with `Generic failure`. After a failed
   enumeration the board shows as `Unknown USB Device (Device Descriptor
   Request Failed)`, `VID_0000&PID_0002`; restart that instance instead.
4. **Move the cable to another USB port** on the PC, which re-enumerates the
   board on another host controller.
5. **Remove the device node** and let Windows create it again, elevated:

   ```powershell
   pnputil /remove-device "USB\VID_0483&PID_5740\3257327B3534"
   pnputil /scan-devices
   ```

   The instance ID is the board's; list them as shown below. Once a node is in
   `CM_PROB_FAILED_START`, this can be the only step that brings it back.

Confirm recovery:

```powershell
Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like '*VID_0483&PID_5740*' } |
    Select-Object Status, Problem, FriendlyName
Get-ItemProperty 'HKLM:\HARDWARE\DEVICEMAP\SERIALCOMM'
```

`Status: OK` and `Problem: CM_PROB_NONE`, and the port listed under
`SERIALCOMM`, mean it is usable again.

### The COM number changes

Windows names a USB device node after the device's serial number when it
accepts one, and otherwise after the hub port. The firmware reports a serial
number built from the chip's unique ID (`Get_SerialNum()` in
`USB_Device/App/usbd_desc.c`), and that node holds `COM4`. After a failed
enumeration Windows may create a port-named node instead, with its own number
(`COM5`-`COM9` exist on the workstation from such failures). To list all
nodes, including disconnected ones (`CM_PROB_PHANTOM`):

```powershell
Get-PnpDevice | Where-Object { $_.InstanceId -like '*VID_0483&PID_5740*' -or
                               $_.InstanceId -like '*VID_0000&PID_0002*' } |
    Select-Object Status, Problem, FriendlyName, InstanceId
```

The number does no harm, since `astro_console.py` takes whichever USB Serial
Device is present. `pnputil /remove-device "<instance ID>"` (elevated) removes
a stale node and frees its number.

### Reduce how often it happens

- Wait a few seconds after a flash or reset before opening the port.
- Close every holder of the port before flashing.
- Prefer `-hardRst` to `-rst`.

The firmware side of the port, including why the USB interrupt has priority 1,
is in [Console.md](../HostControllerA/Docs/Console.md#usb-device); how the
failures were traced is in the
[archive](../HostControllerA/Docs/archive/USB_COM_Port_Investigation.md).

## CubeMX Compliance

Each firmware project is a CubeMX project (`HostControllerA.ioc`,
`DisplayController.ioc`). The rules that keep a regeneration from losing work:

- Application code lives in `User/Inc` and `User/Src`, and code used by both
  projects in `firmware/Common`. Each project's top-level `CMakeLists.txt`
  globs `User/Src` and adds the shared sources, so new files need no change
  to generated CMake.
- Generated files (`Core/`, `Drivers/`, `Middlewares/`, `USB_Device/`,
  `cmake/stm32cubemx/`, `ST67W6X_Network_Driver/`, the startup file) are
  changed only inside their `USER CODE BEGIN` / `USER CODE END` sections, and
  those sections only call into `User/`.
- Peripheral settings, DMA, interrupt enables and priorities are changed in
  the `.ioc` and regenerated, never edited in the generated C.

### Settings that must survive regeneration

| Setting | Where | Value and reason |
| --- | --- | --- |
| Interrupt priorities | `.ioc` (NVIC) | Host: USB 1, everything else 3. DisplayController: I2C1 1, everything else 3. See [host](../HostControllerA/Docs/Architecture.md#interrupt-priorities) and [display board](../DisplayController/Docs/Architecture.md#interrupt-priorities). |
| HSI48 clock recovery (host) | `.ioc` (RCC, CRS sync source USB) and `main()`'s `SysInit` user section | The CRS keeps HSI48 within USB's tolerance, synchronised to the host's start-of-frame packets. CubeMX generates `HSI48CalibrationValue = 32`, the midpoint of the 6-bit field on other families; the G0's field is 7 bits, so the user section puts `TRIM` back to 64, its reset value, before USB starts. |
| Display refresh timer | `.ioc` | Host TIM2, DisplayController TIM6: prescaler 15 (1 MHz count), period 3999 (a placeholder the refresh rewrites every pass), auto-reload preload off, update interrupt enabled. See [Display.md](Display.md#refresh-operation). |
| Display SPI DMA | `.ioc` | Host `SPI3_TX` on DMA1 channel 4, DisplayController `SPI1_TX` on DMA1 channel 1: memory to peripheral, byte to byte, memory increment, normal mode, priority high, interrupt enabled. `HAL_SPI_Transmit_DMA()` finishes in the transfer-complete interrupt, so without it the next transfer is refused as busy. |
| ST67 driver tasks (host) | `USER CODE BEGIN EC` in `ST67W6X_Network_Driver/Target/w61_driver_config.h` | `SPI_THREAD_STACK_SIZE` 1536 (default 768), `SPI_THREAD_PRIO` 46 and `W61_MDM_RX_TASK_PRIO` 47 (defaults 53, 54). CubeMX does not expose them, and definitions on the CMake target do not reach the driver, which is compiled in the generated `STM32_Drivers` library. The stack is needed; the priorities were lowered while the display refresh was a task and are harmless now that it runs from an interrupt. |
| Retained RAM (host) | `USER` lines in `STM32G0B1xx_FLASH.ld` | `RAM` 139 KiB and a 5 KiB `NOINIT` region at `0x20022C00` for the error log ([Console.md](../HostControllerA/Docs/Console.md#error-log)). CubeMX regenerates the linker script only when asked to; if it does, re-apply both, or the log silently stops surviving resets. |
| Application start | `RTOS_THREADS` user section of `main.c` | Calls `AstroWeather_Init()` or `DisplayController_Init()` before the scheduler starts. `defaultTask`, which CubeMX will not remove, exits from its user section. |
| USB console bridge (host) | User sections of `USB_Device/App/usbd_cdc_if.c` | Pass received bytes and the line-state and line-coding requests to `ConsoleService`. |

A misplaced override in a driver header fails silently. Check one with a
preprocessor dump of a driver unit, for example `spi_iface.c` from
`compile_commands.json` with `-E -dM`.

### After a regeneration

```bash
git diff --check
git diff --stat
git diff --name-status
git diff -- *.ioc Core cmake/stm32cubemx
```

Then build `Debug` and `Release` of the regenerated project, the other
project's `Debug` if `firmware/Common` changed, and run the native tests.
Check that the settings in the table above are still in place.

## Validation Checklist

Use the narrowest checks that cover the change:

1. Application changes are in `User/`, `firmware/Common` or a user-code
   section.
2. Build the affected preset of every affected project (both, for
   `firmware/Common`), and check the memory summary.
3. Run the native tests of the affected projects.
4. Flash, wait for enumeration, and check the welcome line shows the new build
   time: `python tools/astro_console.py capture --duration 10`.
5. Send `status` and the commands relevant to the change, and keep the output.
6. For communication or timing changes, run `stats on` long enough to see
   several reports: `dropped`, `busyDrop`, the stack headroom, and any
   warnings (`errors`).
7. For a DisplayController change, check the board's screens and read
   `g_displayStats` over SWD.

A host-side logic change needs the native tests; a firmware behaviour change
needs at least the build and a console capture from a programmed board. If a
Wi-Fi fetch fails with `[ERR] sem_if_ready not received`, the ST67 module did
not signal ready; see [WiFi.md](../HostControllerA/Docs/WiFi.md).
