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
| | `U101` INA180 + `R101` shunt, `R304`, `R305` (`C306`/`C307` are fitted for the CET6N, see below) |
| | Optional: `Y301` crystal (firmware uses HSI), LEDs, buttons, `U301` EEPROM, `R301`/`R302` (DNP) |

The `MCP6006` + `BC847` stages (`U503`/`U507`/`U511`/`U515`, `Q506`-`Q509`) are required. They load the SCT drivers' `REXT` pins from `LED_BRIGHTNESS`. Without them the displays stay dark.

## Decisions

| Item | Decision |
|---|---|
| `L601` | Bourns `SRR6838A-330M` (33 µH, Isat 1.02 A). A tight fit on the SRP7028A footprint, but proven on the prototype. Kept at 33 µH for its lower ripple (≈ 60 mA p-p at 5 V → 3.7 V). Considered and rejected: `SRR6838A-100M` (10 µH, Isat 1.72 A, DCR 35 mΩ, Farnell 4655293), the TPS54202 datasheet value, with more saturation margin but ≈ 190 mA p-p ripple |
| `D501` | Not fitted, pads shorted |
| `U601` | `TPS54202DDCR` replaces `TPS54302` (out of stock at TME and withdrawn at Farnell). Pin-compatible, and `EN` floats to enable: confirmed in `Hardware_Review.md` M-2. Rechecked 2026-09-24: TPS54302DDCR still 0 at TME; at Farnell the DDCR is not listed and the DDCT is withdrawn |
| `Q501`-`Q505` | `SI2333CDS-T1-E3` replaces `Si2333DDS` (TME has 1, Farnell lead time 56 weeks) |
| MCU | `STM32G0B1CET6N`, with the [VDDIO2 wiring](#stm32g0b1cet6n-vddio2-wiring). The CET6 ships from 2027-01-18 at Farnell, has a 14-week lead time at TME and a 52-week factory lead time at Mouser (checked 2026-09-24) |
| `R504`/`R507`/`R510`/`R513` | 4k7. With it the host board measured about 3.3 mA per segment in room light, already below the SCT2024's 5–30 mA range; 10k would halve that (`Hardware_Review.md`) |

## STM32G0B1CET6N: VDDIO2 wiring

The STM32G0B1CET6 ships from 2027-01-18 at Farnell and has a 14-week lead
time at TME (checked 2026-09-24). The STM32G0B1CET6N is in stock at Farnell
(3772955). It differs from the CET6 on two pins only, and works the same once
those are wired:

| LQFP48 pin | CET6 | CET6N | Display board |
|---|---|---|---|
| 30 | PC6 | `VSS` | Fit a 0R at `C307` to tie it to GND. Leave `R305` unfitted. |
| 31 | PC7 | `VDDIO2` | Add a wire from `R304`'s MCU-side pad (or `C306`'s) to `+3.3V`, and fit 100 nF at `C306` as its decoupling. **Never fit `R304`**: it would put `VBUS_IN` on `VDDIO2`. |

- With `VDDIO2` tied to `VDD`, every I/O on it behaves as on the CET6, so it
  does not matter which I/Os VDDIO2 supplies.
- USB needs it: on the CET6N, `VDDUSB` (PA11/PA12) is supplied from `VDDIO2`,
  so without the wire USB cannot work.
- Firmware: the USB init already calls `HAL_PWREx_EnableVddUSB()`, which sets
  `PWR_CR2_USV`. The HAL notes that on the G0B1 the USV, IOSV and PVMENUSB bits
  are merged into one `PVM_VDDIO2` field (RM0444 Rev 6), so this should also
  release the VDDIO2 I/Os; confirm it on the first board. Firmware for a CET6N
  board should set the field early in `AppVariant_Init()` (`HAL_PWREx_ConfigPVM()`),
  before those I/Os are used.
- Check the wiring, `VDDIO2` at 3.3 V, and USB enumeration on the first board
  before building the rest.

Other STM32G0 parts in LQFP48 (G030/031/041/050/051/061/070/071/081, G0B0,
G0C1) share the pinout for every pin the display board uses (checked in the
CubeMX pin database). Only the G0B1, G0C1 and G0B0 have USB, and only they
have SPI3; the others would need their own CubeMX project and a lean firmware
(36 KB of RAM on a G07x). The G070CBT6 and G071CBT6 were in stock.

## USB console (optional)

For a USB CDC console on a display board, fit `J106` (GCT USB4216-03-A),
`R102`/`R103` (5k1) and, if available, `D101`/`D102` (low-capacitance ESD
diodes, which USB does not need to work). Leave `R101` unfitted: the PC's VBUS
then only reaches the isolated `VBUS_IN` net, and the board stays powered from
the chain. The rows are in [`Display_Board_BOM.csv`](Display_Board_BOM.csv);
the parts are not in the orders below.

## Shopping lists

Final lists, chosen on cost, from the recheck of all three distributors on
2026-09-24: Farnell and Mouser in the afternoon, TME about two hours earlier
the same day. Two orders: Farnell for the MCU and inductor, TME for the rest.

### Farnell

| Part | Farnell # | Qty | Stock | Price incl. VAT | Total |
|---|---|---|---|---|---|
| STMicroelectronics STM32G0B1CET6N | 3772955 | 7 | 195 | 28.29 (1+) | 198.03 |
| Bourns SRR6838A-330M | 4655301 | 5 | 1,939 | 5.45 (1+) | 27.24 |
| | | | | **Total** | **225.27 zł incl. VAT (≈ 183.15 net), free delivery** |

- The CET6N needs the [VDDIO2 wiring](#stm32g0b1cet6n-vddio2-wiring) on every board.
- 7 MCUs rather than 6: the 7th (28.29 zł) costs less than the 29.99 zł delivery
  charged below 200 zł, and leaves two spares for the pin 31 rework.
- CET6N stock was 245 in the morning and 195 in the afternoon of 2026-09-24,
  so it is selling; Mouser holds 1,891 as a fallback.
- CET6N price breaks (incl. VAT): 10+ 21.60, 25+ 18.89.
- Farnell notice: since 2026-07-01 some EU shipping methods need a valid EU VAT
  number on the account.

### TME

| Part | Qty | Stock | Net price per piece | Net total |
|---|---|---|---|---|
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
| | | | **Subtotal** | **371.39 zł net (≈ 456.8 zł incl. VAT)**, plus 13.90 zł net delivery |

**Both orders: about 568.4 zł net including delivery (≈ 699 zł incl. VAT).**

### Why not Mouser

Mouser (mouser.pl, checked 2026-09-24) has the STM32G0B1CET6N (1,891 in stock,
1+ 21.03, 10+ 16.06), the SRR6838A-330M (841, 1+ 4.35, 10+ 3.59) and most
catalogue parts, but not the SCT drivers or either display, so TME is needed in
any case. Mouser delivers free above 300 zł; below that FedEx costs 105 zł
(DDP: Mouser pays duty and customs). Its listed prices had no VAT added in the
basket; they are compared here as net.

| Option (net, MCU + inductor + whatever TME no longer supplies) | MCUs | Cost |
|---|---|---|
| **Farnell** CET6N ×7 + SRR ×5, TME as above | 7 | **554.54 zł** |
| Mouser CET6N ×10 + SRR ×5, plus TPS54202, MCP1825, SI2333, MCP6006, CL21A475 and B6B moved from TME to reach 300 zł | 10 | 565.45 zł |
| Mouser CET6N ×6 + SRR ×5 alone, with 105 zł delivery | 6 | 624.32 zł |

Mouser was dearer than TME on the shared parts (MCP1825 2.61, MCP6006 0.80,
SI2333 2.12 at 10+, B6B 1.06), except CL21A475 (0.33). Mouser also had 0 of the
STM32G0B1CET6 (52-week factory lead time) and of the PESD5V0U1BA.

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

- **`STM32G0B1CET6N`**, rejected on 2026-09-22 and chosen on 2026-09-24 once the CET6 slipped to 2027; see [VDDIO2 wiring](#stm32g0b1cet6n-vddio2-wiring). ST's alternative pinout, not a part without USB PD. It adds a `VDDIO2` pin and a second UCPD port. Farnell's "no UCPD/FDCAN/CEC/LPUART" listing is wrong. Compared with the CET6 (CubeMX `STM32G0B1C(B-C-E)Tx.xml` vs `...TxN.xml`), only two pins differ:

  | LQFP48 pin | CET6 | CET6N | On our PCB |
  |---|---|---|---|
  | 30 | PC6 | `VSS` | `R305`/`C307`, not fitted on display boards |
  | 31 | PC7 | `VDDIO2` | `R304` to `VBUS_IN` (5 V) and `C306`, not fitted on display boards |

  Pin 30 is harmless. Pin 31 would float, and `VDDIO2` supplies `VDDUSB` and some I/Os, which are cut off while it is unpowered. Which I/Os those are was not checked. Using the CET6N needs a wire from pin 31 (the MCU-side pad of `R304`) to 3.3 V on every board, and `R304` must never be fitted. It saves only about 3 zł per chip. Keep it as a fallback if the CET6 delivery slips.
- **TME `STM32G0B1CET6` / `...CET6N`:** 0 in stock, 13-week lead time (1+ 18.25 / 20.29 net, 10+ 14.24 / 15.68 net).
- **SRR6038-330Y** at TME: sold only as a 1,000-piece reel, business customers only.
- **SRP7028A-330M** at TME: discontinued.
- Farnell does not list the FYM-7571, LFD028BUE or SCT2024 parts.
