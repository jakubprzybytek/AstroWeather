# AstroWeather

The repository layout and what each folder holds are in [README.md](README.md).

## Firmware

Before building, testing, flashing or debugging anything under `firmware/`,
read [firmware/Docs/Development.md](firmware/Docs/Development.md) and follow it:
the toolchain `PATH`, the CMake presets, the native tests, flashing and SWD
reads, the USB CDC console, and the CubeMX rules.

- **Check the MCU before every flash or SWD read** unless you have just seen
  which board the ST-LINK is on
  ([Which Board Is on the ST-LINK](firmware/Docs/Development.md#which-board-is-on-the-st-link)).
  There is one ST-LINK, moved by hand between boards, and the programmer
  writes any image to any STM32G0 without a warning.
- A change under `firmware/Common/` is compiled into both firmwares: build
  and test both.
- Each firmware build adds one to the project's committed `BUILD_NUMBER`;
  commit it with the change it was built from.
