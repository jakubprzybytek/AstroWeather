# HostControllerA Firmware

Firmware for the AstroWeather host controller board: an STM32G0B1 with an
ST67W611M1 Wi-Fi module that fetches an astronomy and weather forecast and
shows it on seven-segment and dot-matrix LED displays.

## What It Does

- Every 6 hours, at 00:10, 06:10, 12:10 and 18:10 local time, it joins Wi-Fi
  and fetches a line-based forecast payload over plain HTTP from the AstroWeather
  server (`../../sst`). A slot missed while the board was off or offline is caught
  up once, and failures are retried with a growing delay.
- It checks the payload's CRC, parses it, and shows it on its own LED board and
  on up to five remote display boards reached over I2C (addresses `0x10`–`0x14`).
- It keeps the date and time in the RTC, which runs from the internal LSI
  oscillator. Each board's LSI error is measured once and stored as a trim, and
  every successful fetch steps the clock to the server's time and logs the drift.
  The time is shown as `HH:MM` on numeric display 3.
- It offers a USB CDC console for logs, status and commands.
- It keeps its settings, including the Wi-Fi credentials and the clock trim, in
  an I2C EEPROM.
- It measures the board's supply current, die temperature and VDDA.

## Hardware

| Part | Role |
| --- | --- |
| STM32G0B1CETx | MCU: Cortex-M0+, 512 KB flash, 144 KB RAM, run at 16 MHz |
| ST67W611M1 | Wi-Fi module, on SPI1 with DMA; the TCP/IP stack (LwIP) runs on the MCU |
| 24AA04 | 512-byte I2C EEPROM for settings, on I2C1 at `0x50` |
| SCT2xxx | LED drivers in one SPI3 daisy chain, multiplexed in five slots |
| INA180A2 | Current-sense amplifier into ADC1 channel 10 (`PB2`) |
| `SWITCH_1`, `SWITCH_2` | Push buttons on `PB12`, `PB13` |
| `LED_1`, `LED_2` | Heartbeat (`PC13`) and switch feedback (`PB9`) |
| USB FS | CDC virtual COM port for the console |

Each display board has four four-digit seven-segment displays and a 5x21 dot
matrix. The full pin map is in [docs/Architecture.md](docs/Architecture.md#peripherals-and-pins).
Hardware issues are tracked in [../../KiCad/Hardware_Review.md](../../KiCad/Hardware_Review.md).

## Firmware Variants

One CMake project builds either image, selected by `FIRMWARE_VARIANT`:

| Variant | State |
| --- | --- |
| `HostController` | The full firmware described here. |
| `DisplayController` | A stub for the remote display boards. It starts only the console task; `LogService` is never started, so console replies are dropped and the port stays silent. There is no I2C slave and no local display yet. |

## Quick Start

1. Copy `Appli/App/app_credentials.h.template` to `Appli/App/app_credentials.h`
   (ignored by git) and set `APP_ST67_HTTP_HOST`, a bare host name, and
   `APP_ST67_HTTP_PATH`, starting with `/`. The SSID and password macros in the
   template are no longer used.
2. Build, flash and open the console as described in
   [docs/Development.md](docs/Development.md), for example with the
   `Debug-HostController` preset.
3. On the console, store the Wi-Fi credentials with
   `wifi set <ssid> <password>`. They are saved to the EEPROM and a refresh
   starts straight away; `status` shows the result.
4. If the board is new, measure and set its clock trim (`time trim <ppm>`) as
   described in [docs/RTC.md](docs/RTC.md).

## Features

| Feature | Document |
| --- | --- |
| Boot sequence, tasks, inter-task communication, pins, tests, unused code | [Architecture.md](docs/Architecture.md) |
| Build, flash, debug, native tests, serial tools | [Development.md](docs/Development.md) |
| USB CDC console, logging and the command reference | [Console.md](docs/Console.md) |
| ST67 Wi-Fi session, HTTP fetch, configuration, failure diagnosis, stress mode | [WiFi.md](docs/WiFi.md) |
| Astro fetch, payload format, board mapping, triggers, schedule, progress bar | [AstroRefresh.md](docs/AstroRefresh.md) |
| Local and remote display boards, encoding, I2C protocol | [Display.md](docs/Display.md) |
| RTC on the LSI, trim, sync from the server, drift | [RTC.md](docs/RTC.md) |
| EEPROM settings and their format | [Settings.md](docs/Settings.md) |
| Current, temperature and VDDA monitor | [CurrentSense.md](docs/CurrentSense.md) |

## Known Limitations

- **Plain HTTP only.** The fetch uses port 80 with no TLS; HTTPS is planned in
  [ST67_HTTPS_Implementation_Plan.md](docs/ST67_HTTPS_Implementation_Plan.md).
- **No firmware version or git hash.** Only the build time is stamped into the
  image and reported by the console.
- **DisplayController is a stub.** See [Firmware Variants](#firmware-variants).
  The remote boards the host writes to therefore do not show anything yet.
- **Switch 2 starts a bench stress batch**, by default 100 Wi-Fi connect and
  HTTP fetch cycles, not a user function. See [WiFi.md](docs/WiFi.md).
- **The switches are not debounced.** Each falling edge sets a flag directly
  from the EXTI interrupt, so a bouncing press can register twice; a second
  refresh request while one runs is rejected as busy.
- **The RTC is lost on power loss.** There is no LSE crystal or backup battery;
  the clock shows `--:--` until the next successful fetch or `time set`.

## Documentation

Current:

- [docs/Architecture.md](docs/Architecture.md)
- [docs/Development.md](docs/Development.md)
- [docs/Console.md](docs/Console.md)
- [docs/WiFi.md](docs/WiFi.md)
- [docs/AstroRefresh.md](docs/AstroRefresh.md)
- [docs/Display.md](docs/Display.md)
- [docs/RTC.md](docs/RTC.md)
- [docs/Settings.md](docs/Settings.md)
- [docs/CurrentSense.md](docs/CurrentSense.md)
- [docs/Firmware-RAM-Usage.md](docs/Firmware-RAM-Usage.md): static RAM breakdown
- [docs/CubeMXCompliance.md](docs/CubeMXCompliance.md): keeping application
  changes out of generated code

Plans and hardware:

- [docs/ST67_HTTPS_Implementation_Plan.md](docs/ST67_HTTPS_Implementation_Plan.md): HTTPS plan
- [docs/USB_PD_Feasibility.md](docs/USB_PD_Feasibility.md): USB Power Delivery study
- [docs/Display_Board_Purchasing.md](docs/Display_Board_Purchasing.md) and
  [Display_Board_BOM.csv](docs/Display_Board_BOM.csv): display board parts
- [../../KiCad/Hardware_Review.md](../../KiCad/Hardware_Review.md): hardware issues

Superseded plans and logs are kept in [docs/archive](docs/archive/README.md).
