# Display Board Purchasing

## Goal

Buy parts for display boards: the `AstroWeather` PCB populated only with the MCU, power supply and displays. These boards receive data over I2C from the host controller through `J102`/`J104`. They do not need USB-C, current sensing, the ST67 Wi-Fi module or the light sensor.

Prices and stock were first checked on 2026-09-22 and last on 2026-09-24. Recheck them before ordering.

## Sources and caveats

- The BOM comes from `KiCad/AstroWeather.kicad_sch` (KiCad 9 `kicad-cli` BOM and netlist export). The per-board BOM is in [`Display_Board_BOM.csv`](Display_Board_BOM.csv).
- `KiCad/Controller.kicad_sch` is not part of the sheet hierarchy and was ignored.
- Mouser blocks automated fetches; it was checked on 2026-09-24 through a browser session on `mouser.pl`.
- TME prices are net. Farnell prices include 23% VAT. Mouser prices are compared as net.
- Quantities below assume 5 boards.

## What is on a display board

A display board gets `VBUS`, `GND`, I2C, `LED_BRIGHTNESS` and `LOW_POWER_ENABLE` from the 10-pin daisy-chain connectors `J102`/`J104`.

| Fitted | Not fitted |
|---|---|
| MCU `U302` + decoupling, SWD `J103`, `J102`/`J104` | ST67 sheet (`U201`, `C201`-`C204`, `R201`) |
| Power Supply sheet (`U601`, `U602`, `L601`, `C601`-`C608`, `R601`, `R602`) | Light Sensor sheet (`Q701`, `R701`-`R705`) |
| Display sheet, including the `MCP6006` + `BC847` current-set stages | USB-C `J106`, `R102`/`R103`, `D101`/`D102`, `J105` |
| | `U101` INA180 + `R101` shunt, `R304`/`C306`, `R305`/`C307` |
| | Optional: `Y301` crystal (firmware uses HSI), LEDs, buttons, `U301` EEPROM, `R301`/`R302` (DNP) |

The `MCP6006` + `BC847` stages (`U503`/`U507`/`U511`/`U515`, `Q506`-`Q509`) are required. They load the SCT drivers' `REXT` pins from `LED_BRIGHTNESS`. Without them the displays stay dark.

## Decisions

| Item | Decision |
|---|---|
| `L601` | Bourns `SRR6838A-330M` (33 µH, Isat 1.02 A). A tight fit on the SRP7028A footprint, but proven on the prototype. Kept at 33 µH for its lower ripple (≈ 60 mA p-p at 5 V → 3.7 V). Considered and rejected: `SRR6838A-100M` (10 µH, Isat 1.72 A, DCR 35 mΩ, Farnell 4655293), the TPS54202 datasheet value, with more saturation margin but ≈ 190 mA p-p ripple |
| `D501` | Not fitted, pads shorted |
| `U601` | `TPS54202DDCR` replaces `TPS54302` (out of stock at TME and withdrawn at Farnell). Pin-compatible, and `EN` floats to enable: confirmed in `Hardware_Review.md` M-2. Rechecked 2026-09-24: TPS54302DDCR still 0 at TME; at Farnell the DDCR is not listed and the DDCT is withdrawn |
| `Q501`-`Q505` | `SI2333CDS-T1-E3` replaces `Si2333DDS` (TME has 1, Farnell lead time 56 weeks) |
| MCU | `STM32G070CBT6`, no USB; see [MCU](#mcu-stm32g070cbt6). The STM32G0B1CET6 ships from 2027-01-18 at Farnell, has a 14-week lead time at TME and a 52-week factory lead time at Mouser (checked 2026-09-24) |
| `R504`/`R507`/`R510`/`R513` | 4k7. With it the host board measured about 3.3 mA per segment in room light, already below the SCT2024's 5–30 mA range; 10k would halve that (`Hardware_Review.md`) |

## MCU: STM32G070CBT6

Display boards use the **STM32G070CBT6** (128 KB flash, 36 KB RAM, LQFP48),
chosen on 2026-09-24. The STM32G0B1CET6 ships from 2027-01-18 at Farnell, has a
14-week lead time at TME and a 52-week factory lead time at Mouser.

- **Same footprint and pins.** Every pin the display board uses is in the same
  place on all STM32G0 LQFP48 parts (checked in the CubeMX pin database): the SCT
  SPI and control lines, `DISPLAY_1_EN`..`DISPLAY_5_EN`, I2C1 on PA9/PA10,
  `LOW_POWER_EN`, the address straps, the switches, the LEDs and SWD. No wiring
  change. `R304`, `R305`, `C306` and `C307` stay unfitted; pins 30/31 are PC6/PC7
  as on the G0B1.
- **No USB.** The G070 has no USB peripheral. The console runs over USART2
  instead; see [Console over UART](#console-over-uart).
- **Firmware consequences** (the DisplayController firmware is still a stub):
  - It needs its own CubeMX project for the G070 (`.ioc`, startup file, linker
    script); it can no longer be a variant of the G0B1 build. The STM32G0 HAL is
    the same.
  - The SCT chain moves from SPI3 to SPI1, on the same PB3/PB5 pins.
  - The image must fit 36 KB of RAM: no USB, no WiFi, a small RTOS heap or none.
    The display code (`PcbDisplayBoard`, `DisplayCodec`, the I2C protocol) is
    portable.
- **Alternatives considered** (all LQFP48, pin-compatible; stock on 2026-09-24):
  - USB-capable: only the STM32G0B1CET6N was in stock (Farnell, Mouser). It needs
    `VDDIO2` (pin 31) wired to `+3.3V` and pin 30 grounded, because those pins
    are `VDDIO2`/`VSS` on the N package; on this board through `R304`'s MCU-side
    pad, 100 nF at `C306` and a 0R at `C307`, never fitting `R304`. The G0B0 and
    G0C1 were out of stock everywhere.
  - Without USB: STM32G071CBT6 (adds UCPD, which is Type-C power negotiation,
    not USB data), STM32G071C8T6 (64 KB), STM32G051C8T6 (18 KB RAM),
    STM32G030/031 (8 KB RAM). The G070CBT6 is the cheapest with 36 KB of RAM.
    Mouser sells it as `STM32G070CBT6TR` cut tape (25,651 in stock, 10+ 6.91),
    but its delivery charge rules it out for this order.

## Console over UART

Without USB, the development console (`tools/astro_console.py`, the log and
the commands) runs over USART2 through the ST-LINK/V2-1 of the NUCLEO-L152RE used
as the programmer, which appears as COM3 ("STLink Virtual COM Port").

| Display board | Signal | Wire to NUCLEO-L152RE |
|---|---|---|
| `U201` pad 23 (net `ST67_TX`, MCU pin 13) | PA2, USART2 TX | **CN3 RX** |
| `U201` pad 22 (net `ST67_RX`, MCU pin 14) | PA3, USART2 RX | **CN3 TX** |
| `J103` pin 3 | GND | already common through the SWD cable (CN4 pin 3) |

- `U201` (the ST67 module) is not fitted on display boards, so its pads are free;
  MCU pins 13/14 carry the same nets. Both sides are 3.3 V.
- CN3 is the ST-LINK's own USART (UM1724 section 6.8); its labels are from the
  ST-LINK's side.
- On the Nucleo, solder bridges SB13/SB14 (closed by default) also join CN3 to the
  on-board STM32L152's PA2/PA3. Keep the L152 from driving them: erase it (CN2
  jumpers on, erase with STM32CubeProgrammer, jumpers off again), or open
  SB13/SB14.
- For external programming the CN2 jumpers stay off, and SB12 must be open if
  CN4 pin 5 (NRST) is used (UM1724).
- Firmware: USART2 at 115200 8N1 on PA2/PA3 (AF1); then
  `python tools/astro_console.py --port COM3 status`.
- Next PCB revision: `J103` pin 6 is unconnected. Route USART2 TX there (or use
  an 8-pin connector with TX and RX) so no wires are needed.

## Shopping lists

Final lists, chosen on cost, from the recheck of Farnell, TME and Mouser on
2026-09-24. Two orders: Farnell for the inductor, TME for everything else,
including the MCU.

### Farnell

| Part | Farnell # | Qty | Stock | Price incl. VAT | Total |
|---|---|---|---|---|---|
| Bourns SRR6838A-330M | 4655301 | 5 | 1,939 | 5.45 (1+) | 27.24 |
| | | | | Delivery (below 200 zł) | 29.99 |
| | | | | **Total** | **57.23 zł incl. VAT** |

- The SRR6838A is not stocked at TME (only the SRR6038 on 1,000-piece reels).
- Buying the MCU here as well (STM32G070CBT6, 3365393, 32,648 in stock, 10+ 11.01
  incl. VAT) would still leave the order below 200 zł, and costs about 14 zł more
  than buying it at TME.
- Farnell notice: since 2026-07-01 some EU shipping methods need a valid EU VAT
  number on the account.

### TME

| Part | Qty | Stock | Net price per piece | Net total |
|---|---|---|---|---|
| STMicroelectronics STM32G070CBT6 | 10 | 250 | 7.80 (10+) | 78.00 |
| FORYARD FYM-7571AUHR-11 | 15 | 3,615 | 5.50 (10+) | 82.50 |
| WENRUN LFD028BUE-103A | 20 | 1,951 | 3.59 (5+) | 71.80 |
| STARCHIPS SCT2024CSSG | 15 | 14,515 | 2.455 (5+) | 36.83 |
| STARCHIPS SCT2167CSSG | 5 | 1,211 | 2.966 (3+) | 14.83 |
| TI TPS54202DDCR | 5 | 98 | 5.361 (1+) | 26.81 |
| Microchip MCP1825S-3302E/DB | 5 | 2,305 | 2.466 (1+) | 12.33 |
| Vishay SI2333CDS-T1-E3 | 25 | 383 | 1.818 (25+) | 45.45 |
| Nexperia BC847B,215 | 20 | 277,400 | 0.2214 (10+) | 4.43 |
| Samsung CL21A475KAQNNNE (4u7 0805 25 V X5R) | 30 | 333 | 0.3570 (10+) | 10.71 |
| Murata GRM21BR6YA106ME43L (10u 0805 35 V X5R ±20%, `C602`) | 10 | 88 | 0.8439 (10+) | 8.44 |
| CONNFLY DS1024-1*10R0 (TME `ZL263-10SG`, 1×10 right-angle socket, `J102`) | 5 | 1,469 | 0.951 (5+) | 4.76 |
| CONNFLY DS1022-1*20RUF1-1 (TME `ZL211-20KG-S`, 1×20 right-angle header, `J104`) | 5 | 1,919 | 0.714 (5+) | 3.57 |
| JST B6B-PH-K-S (LF)(SN) (`J103`, optional SWD) | 5 | 10,822 | 0.750 (1+) | 3.75 |
| Microchip MCP6006T-E/OT (`U503`/`U507`/`U511`/`U515`) | 20 | 949 | 0.718 (1+) | 14.36 |
| Samsung CL10B104KB8NNNC (100n 0603 50 V X7R ±10%) | 200 | 240,984 | 0.0876 (100+) | 17.52 |
| Murata GRM31CR61E226KE15L (22u 1206 25 V X5R ±10%, `C604`/`C605`) | 10 | 58,634 | 1.330 (10+) | 13.30 |
| | | | **Subtotal** | **449.39 zł net (≈ 552.8 zł incl. VAT)**, plus 13.90 zł net delivery |

- STM32G070CBT6: 10 rather than 6, since 6 at the 1+ price (10.85) cost 65.10 zł
  and 10 at the 10+ price cost 78.00 zł. TME price breaks: 1+ 10.85, 10+ 7.80,
  25+ 6.63.

**Both orders: about 627 zł incl. VAT including delivery (≈ 510 zł net).**

### Why not Mouser

Mouser (mouser.pl, checked 2026-09-24) carries the MCU (`STM32G070CBT6TR` cut
tape, 25,651 in stock, 10+ 6.91; tray 3,488, 10+ 7.96), the SRR6838A-330M (841,
1+ 4.35) and most catalogue parts, but not the SCT drivers or either display, so
TME is needed in any case. Mouser delivers free above 300 zł; below that FedEx
costs 105 zł (DDP: Mouser pays duty and customs), which outweighs its lower MCU
and inductor prices. It was dearer than TME on the shared parts (MCP1825 2.61,
MCP6006 0.80, SI2333 2.12 at 10+, B6B 1.06), except CL21A475 (0.33). Its listed
prices had no VAT added in the basket and are compared here as net.

### TME notes

- Order rules: SCT2024CSSG is sold in multiples of 5. SCT2167CSSG has a minimum of 3. ZL263-10SG and ZL211-20KG-S have a minimum of 5.
- More price breaks (net):
  - FYM-7571AUHR-11: 1+ 6.52, 3+ 5.99, 20+ 5.01
  - LFD028BUE-103A: 1+ 4.06, 25+ 3.07, 100+ 2.43
  - SCT2024CSSG: 25+ 2.154, 100+ 1.935
  - SCT2167CSSG: 10+ 2.651
  - CL21A475KAQNNNE: 100+ 0.2330
  - GRM21BR6YA106ME43L: 1+ 1.8101, 50+ 0.4803. Ten pieces cost less than five at the 1+ price.
  - ZL263-10SG: 20+ 0.755, 100+ 0.573
  - ZL211-20KG-S: 10+ 0.565, 100+ 0.425
  - B6B-PH-K-S (LF)(SN): 10+ 0.661, 25+ 0.608
  - MCP6006T-E/OT: 25+ 0.654 (25 pieces cost 16.35, more than 20 at the 1+ price)
  - CL10B104KB8NNNC: 10+ 0.1325, 1000+ 0.0549, 4000+ 0.0445
  - GRM31CR61E226KE15L: 1+ 2.394
- Lead times when stock runs out: SCT2024CSSG 9 weeks, SCT2167CSSG 14 weeks. TPS54202DDCR: 98 in stock on 2026-09-24 (147 on 2026-09-22), 6,000 expected on 2026-11-10.
- Standard delivery costs 13.90 zł net (17.10 zł incl. VAT). DPD, GLS and InPost cost 15.90 zł net.
- Stock changes seen on 2026-09-24: CL21A475KAQNNNE 1,614 → 333 (30 needed); MCP1825S-3302E/DB 702 → 2,305.
- Alternative for `Q501`-`Q505`: `SI2333CDS-T1-GE3`, 304 in stock, 10+ 2.495 net.
- CL10B104KB8NNNC: 20 are needed for 5 boards (`C101`, `C301`, `C305`, `C603`); 200 are bought as stock for the host board and later builds. 50 V also covers `C603` on VBUS.
- GRM31CR61E226KE15L: a 25 V part rather than 10 V, because an MLCC loses capacitance under DC bias and these are the buck's output capacitors at 3.7 V; the more voltage headroom, the more of the 22 µF is left (`Hardware_Review.md` M-1).
- ZL263-10SG: 1×10, 2.54 mm, 90°, THT, gold-plated contacts, 3 A, height 8.4 mm, lead length 3.2 mm.
- ZL211-20KG-S is a 1×20 header, cut in two for two boards, so 3 are enough for 5 boards. The 5-piece minimum leaves spares.
- `C602`: the previously used `GRM21BR6YA106KE43L` (±10%) has 0 in stock at TME. The ±20% `...ME43L` is the same part otherwise; tolerance does not matter for VBUS bulk decoupling. Alternative: TDK `C2012X5R1V106K125AC` (35 V X5R ±10%, 114 in stock, 1+ 1.578, 10+ 1.100).
- The connectors and `C602` were checked on 2026-09-23, the MCP6006, CL10B104KB8NNNC and GRM31CR61E226KE15L on 2026-09-24. MCP6006T-E/OT has 15,000 more in external stock (2026-11-02).

## Not buying

Dropped from the order: 0805 capacitors 1u ×25, 10u ×5 (`C503`), 10p ×5 (`C504`), 100n ×5 (`C601`) and 47p ×5 (`C608`), and all 0805 resistors: 10R ×20, 1k ×25, 4k7 ×20, 100k ×5 and 19k2 ×5.

## Open items

None. Recheck stock and prices on both carts just before ordering.

Closed:

- **`L601` footprint:** the SRR6838A-330M fits the `L_Bourns_SRP7028A_7.3x6.6mm` pads, tightly, and works on the prototype. `SRP7028A-330M` (Farnell 3373372) would match the footprint exactly if ever needed.
- **TPS54202 swap:** confirmed pin-compatible, with `EN` floating to enable; see the `U601` decision.
- **Faint glow on switched-off digits:** none visible on the prototype host board, whose `Q501`-`Q505` gates are driven straight from 3.3 V GPIOs against a 3.7 V source. The margin is still thin, so a gate driver is listed for the next PCB revision in `Hardware_Review.md` H-3.

## Parts that were rejected

- **`STM32G0B1CET6N`**: the only USB-capable part in stock on 2026-09-24, but it needs `VDDIO2` wired on every board; see [MCU](#mcu-stm32g070cbt6). ST's alternative pinout, not a part without USB PD. It adds a `VDDIO2` pin and a second UCPD port. Farnell's "no UCPD/FDCAN/CEC/LPUART" listing is wrong. Compared with the CET6 (CubeMX `STM32G0B1C(B-C-E)Tx.xml` vs `...TxN.xml`), only two pins differ:

  | LQFP48 pin | CET6 | CET6N | On our PCB |
  |---|---|---|---|
  | 30 | PC6 | `VSS` | `R305`/`C307`, not fitted on display boards |
  | 31 | PC7 | `VDDIO2` | `R304` to `VBUS_IN` (5 V) and `C306`, not fitted on display boards |

  Pin 30 is harmless. Pin 31 would float, and `VDDIO2` supplies `VDDUSB` and some I/Os, which are cut off while it is unpowered. Which I/Os those are was not checked. Using the CET6N needs a wire from pin 31 (the MCU-side pad of `R304`) to 3.3 V on every board, with 100 nF at `C306` and a 0R at `C307`, and `R304` must never be fitted. With `VDDIO2` tied to `VDD` it behaves as the CET6, and its USB works (VDDUSB comes from VDDIO2). Kept as the option if USB on the display boards is ever needed.
- **`STM32G0B1CET6`:** 0 in stock everywhere on 2026-09-24 (Farnell ships from 2027-01-18, Mouser 52-week factory lead time). At TME on 2026-09-22: 0 in stock, 13-week lead time (1+ 18.25 / 20.29 net, 10+ 14.24 / 15.68 net).
- **SRR6038-330Y** at TME: sold only as a 1,000-piece reel, business customers only.
- **SRP7028A-330M** at TME: discontinued.
- Farnell does not list the FYM-7571, LFD028BUE or SCT2024 parts.
