# Firmware Documentation

Documents that apply to more than one firmware project. Each project's own
documents are listed in its README.

| Document | Covers |
| --- | --- |
| [Development.md](Development.md) | Toolchain, building, flashing and debugging every project, connecting to the host's USB CDC console, and keeping application changes CubeMX-compliant |
| [Display.md](Display.md) | The LED display shared by every board: content model, blink and brightness levels, PCB encoding, the interrupt-driven refresh, boot screens and "no data", the `SCT2xxx` chain |
| [I2C.md](I2C.md) | The I2C link between the host and the display boards: bus, addresses and straps, message format, the host's master side and the display board's target side |
| [Testing.md](Testing.md) | Native unit tests: running and writing them, the shared stubs and assertions, CI, coverage, the shared code's suites |
| [Utilities.md](Utilities.md) | The shared tasks, mutexes, switches, CRC-32 and status LEDs, and the rules for code in `Common` |

## Projects

| Project | Board | README |
| --- | --- | --- |
| `HostControllerA` | Wi-Fi host, STM32G0B1 | [README](../HostControllerA/README.md) |
| `DisplayController` | Remote display boards, STM32G070 | [README](../DisplayController/README.md) |
| `Common` | Code compiled into both of the above | [README](../Common/README.md) |
| `Bypass` | Bench USB-to-UART bridge for the ST67 module, STM32G0B0 | [README](../Bypass/README.md) |

The board hardware (schematic review, display board parts, known display
faults) is documented in [KiCad/Docs](../../KiCad/Docs/README.md).
