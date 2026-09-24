# AstroWeather

AstroWeather shows whether the coming nights are good for observing the sky. A
serverless API combines astronomy (sunset, sunrise, sun and moon) with weather
forecasts for a configured location and serves them as a compact text payload.
A Wi-Fi host board fetches that payload every 6 hours and shows six observing
nights on LED displays: its own and up to five chained display boards.

```text
weather supplier ──> sst API (AWS) ──HTTP──> host board ──I2C──> display boards (up to 5)
 (Clearoutside)       GET /astro/{id}       STM32G0B1 +           same PCB, no Wi-Fi
                                            ST67W611M1 Wi-Fi
```

## Repository layout

| Folder | What it holds | Start here |
| --- | --- | --- |
| [`sst/`](sst/) | The server: an [SST](https://sst.dev/) app on AWS (API Gateway, Lambda, DynamoDB, CloudFront), the weather ingestion jobs and a web UI | [architecture](sst/docs/architecture.md) |
| [`firmware/HostControllerA/`](firmware/HostControllerA/) | Firmware for the board, built as the Wi-Fi host controller or as a display controller | [README](firmware/HostControllerA/README.md) |
| [`firmware/Bypass/`](firmware/Bypass/) | Bench firmware for a separate STM32G0B0 board: a USB-to-UART bridge for talking to and flashing the ST67W611M Wi-Fi module | [README](firmware/Bypass/README.md) |
| [`KiCad/`](KiCad/) | Schematic and PCB of the board. One design is populated as the host or as a display board | [hardware review](KiCad/Hardware_Review.md) |

## Documentation

**Server (`sst/docs/`)**

- [architecture.md](sst/docs/architecture.md): components, data flow, storage, deployment stages
- [api.md](sst/docs/api.md) and [api-payload.md](sst/docs/api-payload.md): the forecast endpoint and its text payload, which the firmware parses
- [clearoutside-weather-supplier.md](sst/docs/clearoutside-weather-supplier.md): the weather source in use
- [meteosource-weather-supplier.md](sst/docs/meteosource-weather-supplier.md): evaluation of a possible second source
- [development.md](sst/docs/development.md): setup, local development and deployment
- [testing.md](sst/docs/testing.md): unit and integration tests

**Firmware (`firmware/HostControllerA/`)**

- [README](firmware/HostControllerA/README.md): what the firmware does, a feature status table, quick start, and the list of its documents
- [Development.md](firmware/HostControllerA/docs/Development.md): build, flash, debug and the USB console
- [Console.md](firmware/HostControllerA/docs/Console.md): every console command, including Wi-Fi and server settings

**Hardware (`KiCad/`)**

- [Hardware_Review.md](KiCad/Hardware_Review.md): design review, open issues, and measured current draw
- [Display_Board_Purchasing.md](firmware/HostControllerA/docs/Display_Board_Purchasing.md) and [Display_Board_BOM.csv](firmware/HostControllerA/docs/Display_Board_BOM.csv): parts for the display boards

## Status

- The server is deployed in stages (`prod`, `int`, ...; see
  [architecture.md](sst/docs/architecture.md#web-ui)); the host board fetches
  from the `int` stage.
- The host board fetches, shows and schedules the forecast; the display
  controller firmware is still a stub, so remote boards do not show anything
  yet. See the feature table in the [firmware README](firmware/HostControllerA/README.md#features).
- The prototype host board carries hand rework for hardware issues that are not
  yet in the design files. See [Hardware_Review.md](KiCad/Hardware_Review.md#issues-sorted-by-severity).

## Getting started

- **Server:** [sst/docs/development.md](sst/docs/development.md).
- **Firmware:** the [quick start](firmware/HostControllerA/README.md#quick-start)
  in the firmware README, then [Development.md](firmware/HostControllerA/docs/Development.md)
  to build and flash. The board is pointed at the server with the `api` and
  `wifi` console commands.
