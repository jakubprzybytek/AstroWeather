# Display Board Purchasing

## Goal

Buy parts for display boards: the `AstroWeather` PCB populated only with the MCU, power supply and displays. These boards receive data over I2C from the host controller through `J102`/`J104`. They do not need USB-C, current sensing, the ST67 Wi-Fi module or the light sensor.

Prices and stock were checked on 2026-09-22. Recheck them before ordering.

## Sources and caveats

- The BOM comes from `KiCad/AstroWeather.kicad_sch` (KiCad 9 `kicad-cli` BOM and netlist export). The per-board BOM is in [`Display_Board_BOM.csv`](Display_Board_BOM.csv).
- `KiCad/Controller.kicad_sch` is not part of the sheet hierarchy and was ignored.
- Mouser (`eu.mouser.com`) blocks automated access, so it was not checked.
- TME prices are net. Farnell prices include 23% VAT.
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
| `L601` | Bourns `SRR6838A-330M` (33 µH, Isat 1.02 A) |
| `D501` | Not fitted, pads shorted |
| `U601` | `TPS54202DDCR` replaces `TPS54302` (out of stock at TME and withdrawn at Farnell). Pin-compatible, and `EN` floats to enable: confirmed in `Hardware_Review.md` M-2. Rechecked 2026-09-24: TPS54302DDCR still 0 at TME; at Farnell the DDCR is not listed and the DDCT is withdrawn |
| `Q501`-`Q505` | `SI2333CDS-T1-E3` replaces `Si2333DDS` (TME has 1, Farnell lead time 56 weeks) |
| MCU | `STM32G0B1CET6`, not the `...CET6N` variant (different pins 30/31; see below) |
| `R504`/`R507`/`R510`/`R513` | 4k7. With it the host board measured about 3.3 mA per segment in room light, already below the SCT2024's 5–30 mA range; 10k would halve that (`Hardware_Review.md`) |

## Farnell order

| Part | Farnell # | Qty | Price incl. VAT | Total |
|---|---|---|---|---|
| STM32G0B1CET6 | 4904662 | 6 | 31.56 | 189.37 |
| Bourns SRR6838A-330M | 4655301 | 5 | 5.45 | 27.24 |
| | | | **Total** | **≈ 216.6 zł** |

- The STM32 was orderable but not in stock; shipping was due to start on 2026-09-26. The inductor had 1,939 in stock.
- Standard delivery is free from 200 zł, otherwise 29.99 zł. Express costs 39.99 zł. The 6th STM32 is a spare that keeps the order above 200 zł.
- STM32 price breaks (incl. VAT): 1+ 31.56, 10+ 24.03, 25+ 21.07, 50+ 20.60, 100+ 20.14.
- Farnell notice: since 2026-07-01 some EU shipping methods need a valid EU VAT number on the account.

## TME order

| Part | Qty | Stock | Net price per piece | Net total |
|---|---|---|---|---|
| FORYARD FYM-7571AUHR-11 | 15 | 3,615 | 5.46 (10+) | 81.90 |
| WENRUN LFD028BUE-103A | 20 | 1,951 | 3.57 (5+) | 71.40 |
| STARCHIPS SCT2024CSSG | 15 | 14,515 | 2.439 (5+) | 36.59 |
| STARCHIPS SCT2167CSSG | 5 | 1,211 | 2.947 (3+) | 14.74 |
| TI TPS54202DDCR | 5 | 98 | 5.361 (1+) | 26.81 |
| Microchip MCP1825S-3302E/DB | 5 | 702 | 2.450 (1+) | 12.25 |
| Vishay SI2333CDS-T1-E3 | 25 | 383 | 1.817 (25+) | 45.43 |
| Nexperia BC847B,215 | 20 | 281,924 | 0.2212 (10+) | 4.42 |
| Samsung CL21A475KAQNNNE (4u7 0805 25 V X5R) | 30 | 1,614 | 0.3566 (10+) | 10.70 |
| Murata GRM21BR6YA106ME43L (10u 0805 35 V X5R ±20%, `C602`) | 10 | 88 | 0.8423 (10+) | 8.42 |
| CONNFLY DS1024-1*10R0 (TME `ZL263-10SG`, 1×10 right-angle socket, `J102`) | 5 | 1,469 | 0.945 (5+) | 4.73 |
| CONNFLY DS1022-1*20RUF1-1 (TME `ZL211-20KG-S`, 1×20 right-angle header, `J104`) | 5 | 1,919 | 0.709 (5+) | 3.55 |
| JST B6B-PH-K-S (LF)(SN) (`J103`, optional SWD) | 5 | 10,787 | 0.748 (1+) | 3.74 |
| Microchip MCP6006T-E/OT (`U503`/`U507`/`U511`/`U515`) | 20 | 949 | 0.718 (1+) | 14.36 |
| Samsung CL10B104KB8NNNC (100n 0603 50 V X7R ±10%) | 200 | 241,184 | 0.0876 (100+) | 17.52 |
| Murata GRM31CR61E226KE15L (22u 1206 25 V X5R ±10%, `C604`/`C605`) | 10 | 58,634 | 1.330 (10+) | 13.30 |
| | | | **Subtotal** | **369.86 zł net (≈ 454.9 zł incl. VAT)** |

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

1. **`L601` footprint:** the PCB uses `L_Bourns_SRP7028A_7.3x6.6mm`, which may not match the SRR6838A pads. Check the SRR6838A datasheet land pattern. `SRP7028A-330M` (Farnell 3373372, 5.09 zł incl. VAT) fits the current footprint.

Closed:

- **TPS54202 swap:** confirmed pin-compatible, with `EN` floating to enable; see the `U601` decision.
- **Faint glow on switched-off digits:** none visible on the prototype host board, whose `Q501`-`Q505` gates are driven straight from 3.3 V GPIOs against a 3.7 V source. The margin is still thin, so a gate driver is listed for the next PCB revision in `Hardware_Review.md` H-3.

## Parts that were rejected

- **`STM32G0B1CET6N`** (Farnell 3772955, 267 in stock, 28.29 zł incl. VAT): ST's alternative pinout, not a part without USB PD. It adds a `VDDIO2` pin and a second UCPD port. Farnell's "no UCPD/FDCAN/CEC/LPUART" listing is wrong. Compared with the CET6 (CubeMX `STM32G0B1C(B-C-E)Tx.xml` vs `...TxN.xml`), only two pins differ:

  | LQFP48 pin | CET6 | CET6N | On our PCB |
  |---|---|---|---|
  | 30 | PC6 | `VSS` | `R305`/`C307`, not fitted on display boards |
  | 31 | PC7 | `VDDIO2` | `R304` to `VBUS_IN` (5 V) and `C306`, not fitted on display boards |

  Pin 30 is harmless. Pin 31 would float, and `VDDIO2` supplies `VDDUSB` and some I/Os, which are cut off while it is unpowered. Which I/Os those are was not checked. Using the CET6N needs a wire from pin 31 (the MCU-side pad of `R304`) to 3.3 V on every board, and `R304` must never be fitted. It saves only about 3 zł per chip. Keep it as a fallback if the CET6 delivery slips.
- **TME `STM32G0B1CET6` / `...CET6N`:** 0 in stock, 13-week lead time (1+ 18.25 / 20.29 net, 10+ 14.24 / 15.68 net).
- **SRR6038-330Y** at TME: sold only as a 1,000-piece reel, business customers only.
- **SRP7028A-330M** at TME: discontinued.
- Farnell does not list the FYM-7571, LFD028BUE or SCT2024 parts.
