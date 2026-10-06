# AstroWeather Hardware

One KiCad design, `AstroWeather`, is populated two ways: as the host
controller (STM32G0B1, ST67W611M1 Wi-Fi, USB-C, EEPROM, current sense, light
sensor) and as a display board (STM32G070, power supply and displays only).
Both carry the same LED displays: four four-digit seven-segment displays and a
5x21 dot matrix, driven by SCT2xxx chips.

## Design Files

| File | Content |
| --- | --- |
| `AstroWeather.kicad_pro`, `AstroWeather.kicad_sch` | The project and its root sheet, which also holds the USB-C connector and the `J102`/`J104` daisy-chain connectors |
| `HostController.kicad_sch` | MCU (`U302`), switches, LEDs, address straps |
| `Display.kicad_sch` | LED displays, SCT2xxx chain, slot switches, current-set stages |
| `PowerSupply.kicad_sch` | The 3.3 V supply (`U601`, `U602`, `L601`) |
| `LightSensor.kicad_sch` | The light sensor and the `LED_BRIGHTNESS` divider with `LOW_POWER_ENABLE` |
| `ST67W611M1.kicad_sch` | The Wi-Fi module |
| `AstroWeather.kicad_pcb` | The board |
| `AstroWeather.csv` | Bill of materials of the full (host) population |
| `gerber/` | Fabrication outputs |
| `Controller.kicad_sch` | Not used by the root sheet |

## Documents

| Document | Content |
| --- | --- |
| [Hardware_Review.md](Hardware_Review.md) | Design review against the firmware pin map: issues sorted by severity (C-1, H-1, ... referenced from the firmware docs), per-subsystem notes, measured current draw, what to measure on the bench |
| [Display-Issues.md](Display-Issues.md) | Faults seen on the LED displays, such as the prototype's matrix column faults around `U505` |
| [Display_Board_Purchasing.md](Display_Board_Purchasing.md) | Parts for the display-board population, with [Display_Board_BOM.csv](Display_Board_BOM.csv) |
| [archive/](archive/) | [USB_PD_Feasibility.md](archive/USB_PD_Feasibility.md): the study of negotiating more than 5 V over USB Power Delivery |

The firmware for both populations is described in
[firmware/Docs](../../firmware/Docs/README.md).
