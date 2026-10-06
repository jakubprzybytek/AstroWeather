> Archived 2026-10-06. Current state: [Development.md](../../../Docs/Development.md#com-port-troubleshooting) and [Console.md](../Console.md#usb-device).

# USB COM Port Investigation

From September to 2026-10-05 the host's USB CDC port intermittently failed:
error 31 on opening, Code 10 (`CM_PROB_FAILED_START`) after a reset, or the
port coming back under a new COM number. This records what was observed and
how the cause was found. The cause was the USB interrupt's priority, and the
fix was to give it priority 1.

## Observations

- **2026-09-22:** after resets, disabling and re-enabling the device node had
  all failed and the node sat in `CM_PROB_FAILED_POST_START`, moving the cable
  to another USB port on the PC was the only step that worked.
- **2026-09-30:** once COM4's node was in `CM_PROB_FAILED_START`, resets,
  reflashing, re-plugging and moving to another port all came back failed;
  only `pnputil /remove-device` followed by `/scan-devices` (elevated) brought
  it back. The board twice came back as `COM7`, and later as `COM4` again.
  The workstation had accumulated these nodes:

| Instance ID | Port | Kind |
| --- | --- | --- |
| `USB\VID_0483&PID_5740\3257327B3534` | `COM4` | named after the serial number |
| `USB\VID_0483&PID_5740\6&1ECD3977&1&2`, `...&1&3`, `...&1&4` | `COM7`, `COM6`, `COM5` | named after the hub port |
| `USB\VID_0000&PID_0002\...` | none | failed enumerations, `Device Descriptor Request Failed` |

- **2026-10-04:** `-coreReg` and `addr2line` on two firmware stops found
  `vQueueDelete` ← `W6X_Net_DeInit` (a `configASSERT` on a null semaphore
  inside the driver) and, another time, `vApplicationStackOverflowHook`;
  unrelated to the port, but found while checking that the firmware was alive.

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
a scheduled list of commands, log for a fixed time, close). This too was the
cause below; since the fix the port reopens normally.

## Cause and Fix

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

## Ruled Out and Not Tried

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

## Sources

[STM32 USB device enumeration (Stm32World
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

