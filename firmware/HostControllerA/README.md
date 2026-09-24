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
- It keeps its settings, including the Wi-Fi credentials, the server address and
  the clock trim, in an I2C EEPROM.
- It measures the board's supply current, die temperature and VDDA.
- Switch 2 toggles a low-brightness step on every board.

## Hardware

| Part | Role |
| --- | --- |
| STM32G0B1CETx | MCU: Cortex-M0+, 512 KB flash, 144 KB RAM, run at 16 MHz |
| ST67W611M1 | Wi-Fi module, on SPI1 with DMA; the TCP/IP stack (LwIP) runs on the MCU |
| 24AA04 | 512-byte I2C EEPROM for settings, on I2C1 at `0x50` |
| SCT2xxx | LED drivers in one SPI3 daisy chain, multiplexed in five slots |
| INA180A2 | Current-sense amplifier into ADC1 channel 10 (`PB2`) |
| `SWITCH_1`, `SWITCH_2` | Push buttons on `PB12` (astro refresh) and `PB13` (low brightness) |
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
   `APP_ST67_HTTP_PATH`, starting with `/`. These are the built-in server; a
   board can be pointed elsewhere later with `api host` and `api path`. The SSID
   and password macros in the template are no longer used.
2. Build, flash and open the console as described in
   [docs/Development.md](docs/Development.md), for example with the
   `Debug-HostController` preset.
3. On the console, store the Wi-Fi credentials with
   `wifi set <ssid> <password>`. They are saved to the EEPROM and a refresh
   starts straight away; `status` shows the result.
4. If the board is new, measure and set its clock trim (`time trim <ppm>`) as
   described in [docs/RTC.md](docs/RTC.md).

## Features

Status: ✅ done · 🟡 partial · 🔴 not started · ⚠️ blocked by hardware.
Hardware issue IDs (C-1, H-1, ...) refer to
[Hardware_Review.md](../../KiCad/Hardware_Review.md).

| Feature | Status | Notes | Document |
| --- | --- | --- | --- |
| **Connectivity** | | | |
| Wi-Fi connection (ST67W611M1): join, DHCP, failure diagnosis | ✅ Done | Credentials are set with `wifi set` and saved to the EEPROM | [WiFi.md](docs/WiFi.md) |
| Fetch data over HTTP | ✅ Done | Plain HTTP on port 80 only | [WiFi.md](docs/WiFi.md) |
| Server host and path from the console | ✅ Done | `api host`, `api path`, saved; the built-in values from `app_credentials.h` are the fallback | [WiFi.md](docs/WiFi.md#server) |
| Fetch data over HTTPS | 🔴 Not started | Planned; mbedTLS is not linked | [HTTPS plan](docs/ST67_HTTPS_Implementation_Plan.md) |
| Module power saving between fetches | 🔴 Not started | The module and LwIP stay up between fetches | [WiFi.md](docs/WiFi.md#open-items) |
| **Astro data** | | | |
| Payload parser (protocol 1, 6 blocks) | ✅ Done | Unit tested | [AstroRefresh.md](docs/AstroRefresh.md) |
| Refresh every 6 hours with retry and catch-up | ✅ Done | The last success is lost on power loss | [AstroRefresh.md](docs/AstroRefresh.md#schedule) |
| Refresh from switch 1 or the console | ✅ Done | The switch is debounced only in hardware (RC); a second press while busy is rejected | [AstroRefresh.md](docs/AstroRefresh.md) |
| **Display** | | | |
| Local LED board (multiplexing, progress bar) | ⚠️ Works, HW issue | Off digits glow because the slot P-FETs do not fully turn off (H-3) | [Display.md](docs/Display.md) |
| Sending data to the 5 remote boards over I2C | ✅ Done (host side) | Needs I2C pull-ups, which are `dnp` in the schematic (H-4) | [Display.md](docs/Display.md#i2c-transport) |
| DisplayController firmware for the remote boards | 🔴 Stub | Only the console task starts and its replies are dropped; no I2C slave, no local display | [Development.md](docs/Development.md#firmware-variants) |
| Low-brightness step (`LOW_POWER_ENABLE`) | ✅ Done | `display low on\|off` or switch 2, both saved, for all boards; it still follows the light sensor. Cuts LED current by about half (measured 59–65 → 28 mA with all LEDs lit). Remote boards need the DisplayController firmware from 2026-09-24, which releases their `PB8` (M-4) | [Display.md](docs/Display.md#low-brightness) |
| Numeric formatting (fixed point, time, `?`) | ✅ Done | -0.5 °C shows as `-0.5`; values that do not fit 4 digits show the error pattern | [Display.md](docs/Display.md#fixed-point-values) |
| **Time** | | | |
| RTC clock on numeric display 3, `time` commands | ✅ Done | Lost on power loss (no LSE crystal or backup battery) | [RTC.md](docs/RTC.md) |
| Clock sync from the server | ✅ Done | | [RTC.md](docs/RTC.md#sync-from-the-api) |
| Clock trim | 🟡 Partial | Set by hand with `time trim`; automatic trim is planned | [RTC.md](docs/RTC.md) |
| **Power and sensing** | | | |
| Current, temperature and VDDA monitor | ✅ Done (reworked board) | Works on the prototype with `VREF+` rewired to VDD; the schematic and PCB still tie it to GND (C-1). Current sense also relies on the PC6→PB2 connection (H-1) | [CurrentSense.md](docs/CurrentSense.md) |
| VBUS voltage sense | ⚠️ Blocked by HW | PC7 is not an ADC pin and has no divider (H-1, H-2) | [Hardware review](../../KiCad/Hardware_Review.md) |
| USB-PD negotiation for more than 5 V | 🔴 Not implemented | Feasibility study only; the hardware needs changes | [USB_PD_Feasibility.md](docs/USB_PD_Feasibility.md) |
| Reading the USB-C current limit (CC pins) | 🔴 Not implemented | Worst-case load exceeds the USB default (H-5) | [Hardware review](../../KiCad/Hardware_Review.md) |
| **System** | | | |
| USB console and logging | ✅ Done | | [Console.md](docs/Console.md) |
| EEPROM settings | ✅ Done | A read failure is reported as "blank" | [Settings.md](docs/Settings.md) |
| Firmware version and git hash | 🔴 Not started | Only the build time is stamped | [Development.md](docs/Development.md) |
| Unit tests | 🟡 Partial | 19 native suites, 95% line coverage of the code they compile, run in CI; console commands and the EEPROM store are next (phases 3-4) | [Testing.md](docs/Testing.md) |

## Known Limitations

- **The prototype host board carries hand rework** that the design files do not
  show yet: `VREF+` rewired to VDD (C-1) and the current-sense net taken to PB2
  (H-1). Boards built from the current files need the same changes.

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
- [docs/Testing.md](docs/Testing.md): native unit tests, coverage and the test plan
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
