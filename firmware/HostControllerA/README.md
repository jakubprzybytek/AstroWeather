# HostControllerA Firmware

Firmware for the AstroWeather host controller board: an STM32G0B1 with an
ST67W611M1 Wi-Fi module that fetches an astronomy and weather forecast and
shows it on seven-segment and dot-matrix LED displays, its own and those of up
to five remote display boards.

## What It Does

- Every 6 hours, at 00:10, 06:10, 12:10 and 18:10 local time, it joins Wi-Fi
  and fetches a line-based forecast payload over HTTPS from the AstroWeather
  server (`../../sst`), sending the device key saved with `api key`. A slot
  missed while the board was off or offline is caught up once, and failures
  are retried with a growing delay.
- It checks the payload's CRC, parses it, and shows it on its own LED board and
  on the remote display boards over I2C. Night *n* of the forecast goes to the
  board at address `0x10 + n`; every board, the host included, reads its
  address from its straps. It keeps the display boards' refresh in step with
  its own, so blinking and the `LED_1` heartbeat run together on every board.
- It keeps the date and time in the RTC, which runs from the internal LSI
  oscillator, trimmed per board and stepped to the server's time on every
  fetch. The time is shown as `HH:MM` on numeric display 3.
- It offers a USB CDC console for logs, status and commands, and keeps the
  last 24 warnings and errors over resets.
- It keeps its settings, including the Wi-Fi credentials, the server address
  and key and the clock trim, in an I2C EEPROM.
- It measures the board's supply current, die temperature and VDDA.
- Switch 1 starts a refresh; switch 2 toggles a low-brightness step on every
  board.

## Hardware

| Part | Role |
| --- | --- |
| STM32G0B1CETx | MCU: Cortex-M0+, 512 KB flash, 144 KB RAM, run at 16 MHz |
| ST67W611M1 | Wi-Fi module, on SPI1 with DMA; runs ST's T01 firmware, so TCP/IP, DNS and TLS are in the module |
| 24AA04 | 512-byte I2C EEPROM for settings, on I2C1 at `0x50` |
| SCT2xxx | LED drivers in one SPI3 daisy chain, multiplexed in five slots |
| INA180A2 | Current-sense amplifier into ADC1 channel 10 (`PB2`) |
| `SWITCH_1`, `SWITCH_2` | Push buttons on `PB12` (astro refresh) and `PB13` (low brightness) |
| `LED_1`, `LED_2` | Heartbeat from the display refresh (`PC13`); switch presses and USB console traffic (`PB9`) |
| USB FS | CDC virtual COM port for the console |

The full pin map and the interrupt priorities are in
[Architecture.md](Docs/Architecture.md#peripherals-and-pins). The board design
and its known issues are in [KiCad/Docs](../../KiCad/Docs/README.md).

## Quick Start

1. Copy `Appli/App/app_credentials.h.template` to `Appli/App/app_credentials.h`
   (ignored by git) and set `APP_ST67_HTTP_HOST`, a bare host name, and
   `APP_ST67_HTTP_PATH`, starting with `/`. These are the built-in server; a
   board can be pointed elsewhere with `api host` and `api path`. The SSID and
   password macros in the template are not used.
2. Build, flash and open the console as described in
   [Development.md](../Docs/Development.md).
3. On the console, store the Wi-Fi credentials and the device key; both are
   saved to the EEPROM:

   ```text
   wifi set <ssid> <password>
   api key <key>
   ```

   `wifi set` runs a refresh straight away; `status` shows the result.
4. If the board is new, measure and set its clock trim (`time trim <ppm>`) as
   described in [RTC.md](Docs/RTC.md#measuring-the-drift).

## Features

Status: ✅ done · 🟡 partial · 🔴 not started · ⚠️ blocked by hardware.
Hardware issue IDs (C-1, H-1, ...) refer to
[Hardware_Review.md](../../KiCad/Docs/Hardware_Review.md).

| Feature | Status | Notes | Document |
| --- | --- | --- | --- |
| **Connectivity** | | | |
| Wi-Fi connection (ST67W611M1): join, DHCP, failure diagnosis | ✅ | Credentials set with `wifi set`, saved to the EEPROM | [WiFi.md](Docs/WiFi.md) |
| Fetch over HTTPS | ✅ | TLS in the module (T01), certificate chain and hostname verified against the built-in Amazon Root CA 1; 100/100 stress cycles | [WiFi.md](Docs/WiFi.md#http) |
| Server host, path and device key from the console | ✅ | `api host`, `api path`, `api key`, saved; `app_credentials.h` gives the built-in fallback | [WiFi.md](Docs/WiFi.md#server) |
| Module power saving between fetches | 🔴 | The module stays initialized between fetches | [WiFi.md](Docs/WiFi.md#open-items) |
| **Astro data** | | | |
| Payload parser (protocol 3, 6 blocks) | ✅ | Unit tested | [AstroRefresh.md](Docs/AstroRefresh.md) |
| Refresh every 6 hours with retry and catch-up | ✅ | The last success is lost on power loss | [AstroRefresh.md](Docs/AstroRefresh.md#schedule) |
| Refresh from switch 1 or the console | ✅ | A second request while busy is rejected | [AstroRefresh.md](Docs/AstroRefresh.md#triggers) |
| **Display** | | | |
| Local LED board: interrupt-driven multiplexing, refresh progress bar | ✅ | Matrix column 1 stays lit on the prototype ([Display-Issues.md](../../KiCad/Docs/Display-Issues.md)) | [Display.md](../Docs/Display.md) |
| Blinking and four brightness levels per segment and pixel | ✅ | The forecast's matrix rows carry levels and blink; the clock's colon and the progress bar blink | [Display.md](../Docs/Display.md#blink-and-brightness-levels) |
| Numeric formatting: fixed point, time, `?`, "no data" | ✅ | -0.5 shows as `-0.5`; values that do not fit four digits show the error pattern | [Display.md](../Docs/Display.md#numeric-representation) |
| Boot screens: slot test, then the host's strap address | ✅ | Shared with the display boards | [Display.md](../Docs/Display.md#boot-screens) |
| Remote display boards over I2C | ✅ | Board `0x11` runs the [DisplayController](../DisplayController/README.md) firmware; I2C pull-ups needed (H-4) | [I2C.md](../Docs/I2C.md) |
| Timeline sync: the display boards' refresh, blinking and heartbeat in step with the host's | 🟡 | `DisplaySync` broadcasts on the I2C general call at boot, on request and every 5 min, and polls each board's status; `time sync [now]`. Unit tested and simulated; not yet checked with a display board | [Display.md](../Docs/Display.md#timeline-sync) |
| Heartbeat on `LED_1`, 20 ms every 2 s | ✅ | From the refresh interrupt, so it shows the refresh is running | [Display.md](../Docs/Display.md#the-timeline-on-every-board) |
| Low-brightness step (`LOW_POWER_ENABLE`) for every board | ✅ | `display low on\|off` or switch 2, saved; cuts LED current by about half | [Display.md](Docs/Display.md#low-brightness) |
| **Time** | | | |
| RTC clock on numeric display 3, `time` commands | ✅ | Lost on power loss (no LSE crystal or backup battery) | [RTC.md](Docs/RTC.md) |
| Clock sync from the server, with drift measurement | ✅ | | [RTC.md](Docs/RTC.md#sync-from-the-api) |
| Clock trim | 🟡 | Set by hand with `time trim`; automatic trim not started | [RTC.md](Docs/RTC.md#open-items) |
| HSI16 trim, measured against the PC | ✅ | `time hsi <0-127>`, saved and applied at boot; `tools/hsi_measure.py` suggests the value | [Display.md](../Docs/Display.md#trimming-the-hosts-hsi) |
| **Power and sensing** | | | |
| Current, temperature and VDDA monitor | ✅ (reworked board) | Needs the C-1 and H-1 rework | [CurrentSense.md](Docs/CurrentSense.md) |
| VBUS voltage sense | ⚠️ | PC7 is not an ADC pin and has no divider (H-1, H-2) | [Hardware review](../../KiCad/Docs/Hardware_Review.md) |
| USB-PD negotiation for more than 5 V | 🔴 | Hardware changes needed; see the [study](../../KiCad/Docs/archive/USB_PD_Feasibility.md) | |
| Reading the USB-C current limit (CC pins) | 🔴 | Worst-case load exceeds the USB default (H-5) | [Hardware review](../../KiCad/Docs/Hardware_Review.md) |
| **System** | | | |
| USB console: commands, help, log, statistics | ✅ | `help`, `help <group> <command>`, `help all` | [Console.md](Docs/Console.md) |
| Display commands for this board, a remote board or all | ✅ | `display 0x12 test`, `display all clear` | [Console.md](Docs/Console.md#display) |
| Error log: the last 24 warnings and errors, kept over resets | ✅ | `errors [clear]`; lost on power loss | [Console.md](Docs/Console.md#error-log) |
| EEPROM settings | ✅ | | [Settings.md](Docs/Settings.md) |
| Interrupt priorities: USB 1, everything else 3 | ✅ | | [Architecture.md](Docs/Architecture.md#interrupt-priorities) |
| Firmware version and git hash | 🔴 | Only the build time is stamped | [Architecture.md](Docs/Architecture.md#shared-code) |
| Unit tests | 🟡 | 16 native suites run in CI; the console, the settings store and the remote boards are not covered yet | [Testing.md](Docs/Testing.md) |

## Known Limitations and Open Items

Hardware:

- **The prototype host board carries hand rework** that the design files do not
  show yet: `VREF+` rewired to VDD (C-1) and the current-sense net taken to PB2
  (H-1). Boards built from the current files need the same changes.
- **Random contents at power-up (L-3):** the host still starts `SCT_ENABLE`
  low; the display boards already start it high
  ([Display-Issues.md](../../KiCad/Docs/Display-Issues.md#other-known-issues)).

Firmware, with details in each document's open items:

- Wi-Fi: module power policy, repeated cold restarts, the HTTP error paths at
  run time, small heap retentions, no fetch cancellation, certificate validity
  dates ([WiFi.md](Docs/WiFi.md#open-items)).
- Astro refresh: stale data after a failed noon refresh, low-power wake
  ([AstroRefresh.md](Docs/AstroRefresh.md#open-items)).
- Clock: automatic trim ([RTC.md](Docs/RTC.md#open-items)).
- Display timeline sync: locks within a burst on the bench, but the boards
  drift 76-81 ms apart over the 5-minute gap, so a board asks for a burst
  about every 6 minutes
  ([DisplayController open items](../DisplayController/Docs/Architecture.md#open-items)).
- Settings: a torn write is detected but not recovered, credentials are stored
  in the clear, unknown tags are not preserved
  ([Settings.md](Docs/Settings.md#limitations)).
- RAM: the forecast payload is held twice; holding it once would save 4 KB
  ([Firmware-RAM-Usage.md](Docs/Firmware-RAM-Usage.md)).
- Tests: the console, `Settings::Store`, the EEPROM driver and
  `BufferedDisplayBoard` ([Testing.md](Docs/Testing.md#not-covered-yet)).

## Documentation

This project ([Docs](Docs/)):

- [Architecture.md](Docs/Architecture.md): boot, tasks, inter-task communication, interrupt priorities, pins, memory
- [Console.md](Docs/Console.md): the USB device, the log, the error log and every console command
- [WiFi.md](Docs/WiFi.md): the ST67 module, the session, HTTPS, the server settings
- [AstroRefresh.md](Docs/AstroRefresh.md): the refresh pipeline, the payload, the display mapping, the schedule
- [Display.md](Docs/Display.md): the host's side of the displays: the aggregate display, remote boards, progress bar, low brightness
- [RTC.md](Docs/RTC.md): the clock, its trim and the sync from the server
- [Settings.md](Docs/Settings.md): the EEPROM settings image
- [CurrentSense.md](Docs/CurrentSense.md): current, temperature and VDDA
- [Testing.md](Docs/Testing.md): this project's suites, coverage and bench tests
- [Firmware-RAM-Usage.md](Docs/Firmware-RAM-Usage.md): static RAM breakdown
- [archive](Docs/archive/README.md): plans, investigations and measurement logs

Shared with the display boards ([firmware/Docs](../Docs/README.md)):

- [Development.md](../Docs/Development.md): build, flash, debug, connecting over USB, CubeMX rules
- [Display.md](../Docs/Display.md): the display, its encoding and refresh, the timeline sync, trimming the host's HSI
- [I2C.md](../Docs/I2C.md): the bus, the messages, the addresses, both sides of the link
- [Testing.md](../Docs/Testing.md): the native test kit
- [Utilities.md](../Docs/Utilities.md): tasks, mutexes, switches, the microsecond clock, HSI trim, LEDs
