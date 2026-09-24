# AstroWeather Hardware Design Review

**Date:** 2026-09-23
**Scope:** `KiCad/AstroWeather.kicad_sch` and its sub-sheets (`HostController`, `Display`, `PowerSupply`, `LightSensor`, `ST67W611M1`), cross-checked against the firmware pin map (`firmware/HostControllerA/Core/Inc/main.h`, `HostControllerA.ioc`, `Core/Src/main.c`) and the hardware notes in `firmware/HostControllerA/docs/`.

## Method and limits

- The netlist and BOM were exported read-only with KiCad 9 `kicad-cli` (`sch export netlist`, `sch export bom`, `sch erc`) into a scratch folder, then parsed with a throwaway Python script. No project file was modified.
- For a few key parts (`U302`, `U601`, `Q501`, `R304`, `R305`), the net assignments on the PCB pads were read from `AstroWeather.kicad_pcb`. **The PCB layout itself (placement, copper, antenna keep-out, thermal, return paths) was not reviewed.**
- The `STM32G0B1C(B-C-E)Tx` pin positions and alternate functions come from the STM32CubeMX MCU database installed with STM32CubeIDE 1.19.
- Part values come from the schematic `Value` fields. The schematic has **no MPN or manufacturer fields**. Where a value is missing or a placeholder (`dnp`, `x`, `4k7 / 10k`, `D_TVS`), this review says so and does not guess.
- `KiCad/Controller.kicad_sch` (an STM32G030F6 sheet) is not part of the hierarchy and was not reviewed.
- The same PCB is built in two ways: as the **Host Controller** (fully populated) and as up to five **Display Controllers** (no USB-C, INA180, ST67 or light sensor; see `Display_Board_Purchasing.md`). Findings note which build they affect.
- Datasheets used:
  - SCT2024: [StarChips SCT2024 (TME copy)](https://www.tme.eu/Document/8876e3da3e0cc25d8b4c7cdeea8b8a88/SCT2024.pdf)
  - TPS54202: [TI SLVSD26C](https://www.ti.com/lit/ds/symlink/tps54202.pdf)
  - Si2333CDS: [Vishay 68717](https://www.vishay.com/docs/68717/si2333cd.pdf)
  - ST67W611M1 data brief: [DB5452](https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/7049/DB5452_ST67W611M1.pdf) and the [ST wiki on power supply](https://wiki.st.com/stm32mcu/wiki/Connectivity:ST67W611M1_Power_supplies_and_grounding) (search summary only; the full ST67 datasheet could not be downloaded)
  - FYM-7571AUHR-11 parameters: [TME product page](https://www.tme.eu/en/details/fym-7571auhr-11/led-displays-matrix/foryard/)
  - LFD028BUE-103A: TME/Amazon listings (red, 640 nm, common anode). The Wenrun datasheet itself was not available.
  - Other limits (MCP1825S, INA180, MCP6006, 24AA04/AT24CS01, USB) are quoted from memory and marked "needs datasheet check" where they drive a conclusion.

## Power tree

```
J106 USB-C VBUS (5 V) ──┬── J105 AUX_POWER pin 2   (same node, no protection)
      "VBUS_IN"         ├── R304 (10k sch / 100k PCB) ──> PC7 + C306 1u      (no divider, no ADC on PC7)
                        └── R101 50 mΩ shunt ── U101 INA180A2 (G=50, V+ = 3.3 V)
                               │                    └─ OUT ─ R305 (10k sch / 100k PCB) ─ C307 1u ─> PC6 (no ADC; bodged to PB2)
                               ▼
                             "VBUS" ─┬── J102/J104 pins 6,8 ──> up to 5 Display Controllers (each has its own buck + LDO)
                                     └── U601 TPS54302 (TPS54202 on new boards), C602 10u/35V + C603 100n
                                           L601 33 µH, R601 100k / R602 19k2, C608 47p, C604+C605 2×22u
                                           ▼
                                        "VDD" = 0.596 × (1 + 100/19.2) = 3.70 V  (3.61–3.79 V over the Vref tolerance)
                                           ├── D501 (fitted as a short) ──> VDD_LED ──> Q501–Q505 Si2333 ──> VDD_DIG1..5
                                           │                                          ──> LED anodes ──> SCT2024/SCT2167 sinks
                                           └── U602 MCP1825S-3.3 (C606 4u7 in, C607 1u out)
                                                 ▼
                                              "+3.3V" ──> STM32G0B1 VDD/VBAT (C301 100n, C302 4u7), ST67W611M1 VDD33/VDDIO
                                                          (C201–C203 3×10u), INA180 (C101), EEPROM U301, SCT VDD via
                                                          10 Ω + 1u + 4u7 (×4), MCP6006 ×4, LDR divider, J101/J103
```

Ground: single `GND` net. `VREF+` is also on `GND` (see **C-1**; reworked to VDD on the prototype).

### Load estimate (needs measurement)

The LED current is set by the analog `LED_BRIGHTNESS` voltage, not by a fixed REXT (see the Display notes). The SCT2024 formula is `IOUT ≈ 30 × (630 / REXT)` mA. That is 18.9 V divided by the REXT resistance in mA, and 21 mA at 900 Ω (SCT2024 datasheet, p.10). Depending on the internal REXT reference (0.63 V or 1.26 V, not stated), the gain is 30× or 15× the current drawn from REXT. With `R504` = 4k7 and the brightest light-sensor voltage (1.17 V), that gives **3.7–7.5 mA peak per segment**.

| Load | Per board | Notes |
|---|---|---|
| LEDs, all 53 channels lit in a slot (32 numeric + 21 matrix) | 0.20–0.40 A at 3.7 V | One slot is lit at any time, so this is continuous. It halves with `R504` = 10k. |
| SCT ×4 IDD, MCU, op-amps | ≈ 45 mA at 3.3 V | SCT2024 IDD(ON) 7–10 mA each |
| ST67W611M1 TX burst (host only) | ≈ 300–400 mA at 3.3 V | **Not verified.** 21 dBm Wi-Fi 6 module, typical figure. Needs ST67 datasheet check. |
| **Host, from VBUS** | ≈ 0.35–0.7 A | assuming 85% buck efficiency |
| **5 display boards, from VBUS** | ≈ 5 × (0.2–0.4 A) = 1.0–2.0 A | all of it flows through the host's `R101` and `J106` |
| **System worst case** | **≈ 1.4–2.7 A** | above the USB 2.0 limit (500 mA) and the Type-C 1.5 A level. Only a 3 A Type-C source is safe. |

### Measured current (host board, 2026-09-24)

These readings come from the host's own current sense (`adc log on`) on the reworked prototype, firmware built 2026-09-23 17:11:34, with no remote boards connected. They are the VBUS input current through `R101`, at 5 V. The sense path is filtered by `R305`/`C307` (τ = 100 ms) and sampled at 10 Hz (see [`CurrentSense.md`](../firmware/HostControllerA/docs/CurrentSense.md)). **Anything shorter than about 100 ms is averaged out. Instantaneous peaks, such as Wi-Fi TX bursts, are higher than shown.**

**LEDs.** All four numeric displays were set to `8.888` and all 5×21 matrix dots were lit. That is 50 of the 53 channels; `display set` can light only one decimal point per display. The LED current depends on `LED_BRIGHTNESS`, so on the ambient light. The light level was not measured.

| State | Current at 5 V | Notes |
|---|---|---|
| All LEDs off, Wi-Fi idle | 12–17 mA, typ. 14 | MCU, LDO, SCT idle, op-amps, ST67 idle |
| All LEDs on, dimmer light | 53–57 mA, typ. 55 | LEDs ≈ 42 mA, about 1 mA per segment |
| All LEDs on, brighter light (two runs) | 154–163 mA, typ. 157 | LEDs ≈ 143 mA, about 3.3 mA per segment at 3.7 V |

The brighter reading is just below the 3.7–7.5 mA per segment estimated above for 1.17 V on `LED_BRIGHTNESS`. The maximum brightness case has not been measured yet.

**Low brightness (`LOW_POWER_ENABLE` high, `Q701` on).** All LEDs lit as above, firmware built 2026-09-24 10:14:44, alternating `display low off`/`on` three times in the same light:

| State | Current at 5 V | LEDs (minus 14 mA) |
|---|---|---|
| Normal | 73–79 mA | 59–65 mA |
| Low | 42 mA | 28 mA |
| Either, all LEDs off | 13–15 mA | — |

The LED current falls to about 0.45 of normal. The divider ratio alone (1.5 kΩ instead of 4.7 kΩ at the bottom) predicts 0.32–0.42, so the current-set stages do not track `LED_BRIGHTNESS` exactly (M-5). The ratio is likely to change with the light level.

**Astro refresh.** Three `astro refresh` runs were captured with the normal display content shown. Each keeps the ST67 active for about 5 s: joining, getting an IP, downloading, disconnecting. All three succeeded.

| Run | Before refresh | Wi-Fi active, typical | Highest sample | Rise over baseline |
|---|---|---|---|---|
| 1 | 67 mA | 110–120 mA | 144 mA (joining) | +77 mA |
| 2 | 56 mA | 110–135 mA | 161 mA (joining) | +105 mA |
| 3 | 73 mA | 115–130 mA | 162 mA (joining) | +89 mA |

The highest filtered samples came while joining the network; the download stage sat steadier at 125–147 mA. Averaged over 100 ms, the ST67 adds about 45–65 mA while active and at most about 105 mA. That is well below the 300–400 mA TX figure assumed in the load table. Short TX bursts may still reach that level, but they would need a scope across `R101` to see. For `L601` (M-1), the filter hides nothing that matters thermally, but it could hide short saturation peaks.

Log: 1,608 samples, captured with `astro_console.py send --wait 40 "adc log on" "astro refresh" "astro refresh" "astro refresh" "adc log off"`.

## Issues (sorted by severity)

Confidence key:
- **Confirmed:** confirmed from the schematic (and PCB where noted).
- **Measure:** needs a bench measurement.
- **Datasheet:** needs a datasheet check.

| ID | Severity | Sheet / Ref(s) | Issue | Evidence | Suggested fix | Confidence |
|---|---|---|---|---|---|---|
| C-1 | **Critical** | Host Controller / U302 pin 5 | **`VREF+` is tied to GND.** The ADC and DAC have no reference, so every ADC result (current sense, VREFINT, temperature) is meaningless. This very likely explains the unexplained readings in [`ADC_Current_Monitor_Troubleshooting.md`](../firmware/HostControllerA/docs/archive/ADC_Current_Monitor_Troubleshooting.md) (0x0FF/0x1FF, 16·N−1 patterns, no match with the 20–30 mV seen on the scope). | Netlist: `U302.5(VREF+)` is on `GND`. The PCB pad 5 net is `GND`. CubeMX DB: LQFP48 pin 5 = `VREF+`, pin 6 = `VDD` (VDD/VDDA). The STM32G0 datasheet requires VREF+ to be within 2 V…VDDA while the ADC is used (needs datasheet check for the exact limit). | **Reworked on the prototype host board:** pin 5 rewired to VDD, and current sensing now works. The schematic and PCB are not updated yet. Other boards: lift pin 5 or cut its GND connection, then strap it to pin 6 (VDD/VDDA) with 100 nF (+1 µF) to GND. Next revision: route `VREF+` to `+3.3V` with local 100 nF + 1 µF. Then repeat the known-voltage ADC test in the troubleshooting doc. | Confirmed (schematic + PCB); fixed by rework on the prototype |
| H-1 | High | Host Controller / U302 pins 30, 31 | **Both analog sense nets land on pins that have no ADC input.** `CURRENT_SENSE_FLTR` goes to PC6 and `VOLTAGE_SENS_FLTR` goes to PC7. The firmware reads `ADC1_IN10` on PB2 through a bodge from PC6 to PB2 (TP301 / MCO net). PC7 cannot be measured at all. | CubeMX DB: PC6 = LPUART2_TX/TIM2_CH3/TIM3_CH1/UCPD1_FRSTX; PC7 = LPUART2_RX/TIM2_CH4/TIM3_CH2/UCPD2_FRSTX (no ADC). PB2 = ADC1_IN10 and PB0 = ADC1_IN8 (PB0 is unconnected). `main.h`: `CURRENT_SENSE_Pin` = PB2. | Next revision: route current sense to PB2 (IN10) and voltage sense to PB0 (IN8, currently free). Keep PC6/PC7 unconnected or give them other uses. | Confirmed |
| H-2 | High | Host Controller / R304, C306, PC7 | **5 V VBUS goes straight to an MCU pin with no divider.** A pin in analog mode (the `.ioc` sets PC7 to `GPIO_Analog`) must stay below VDDA. The 5 V pushes a small injected current into the pin permanently. Even if it were an ADC pin, it would read full scale. | Netlist: `R304` goes from `VBUS_IN` to `PC7`, `C306` 1u to GND, and there is no bottom resistor. The value is 10k in the schematic and **100k on the PCB**. Already flagged in `USB_PD_Feasibility.md` §4. | Add a bottom resistor to form a divider (for example 100k / 68k, which gives 2.0 V at 5 V and 8.1 V at full scale), and move it to PB0 (see H-1). Current boards: remove R304 until a divider is added. | Confirmed; the injected current is a datasheet check |
| H-3 | High | Display / Q501–Q505, PD3/PA15/PD1/PD2/PD0 | **The slot P-FETs cannot be fully turned off.** The source is on `VDD_LED` = `VDD` ≈ 3.70 V (D501 shorted), and the gate is driven directly by a 3.3 V push-pull GPIO. So Vgs(off) ≈ −0.4 V (range −0.3 … −0.5 V over the tolerances). Si2333CDS Vgs(th) is −0.4 V min … −1.0 V max at 250 µA, and its magnitude falls with temperature. A worst-case part conducts hundreds of µA in the "off" state, which is enough to make "off" digits and matrix rows glow. There is no gate resistor and no gate pull-up to VDD_LED. | Netlist: `Q50x.2(S)` on `VDD_LED`, `Q50x.1(G)` directly on `DISPLAYx_EN` (MCU pins 41/37/39/40/38). Si2333CDS datasheet p.2: VGS(th) −0.4…−1 V, tempco +2.6 mV/°C. `Display_Board_Purchasing.md` open item 4 predicts the same. When on, Vgs ≈ −3.7 V gives RDS(on) ≤ 35–45 mΩ, which is fine. | Pick one: **(a)** configure `DISPLAY_x_EN` as **open-drain** and add 10k pull-ups from each gate to `VDD_LED` (needs PD0–PD3 and PA15 to be FT/5 V-tolerant; check the G0B1 pin table). This also keeps the FETs off during reset. **(b)** Fit a Schottky at D501 so VDD_LED ≈ 3.3–3.4 V. Its Vf falls at low current, so the margin is small. **(c)** Next revision: add an NPN/N-FET gate driver with a gate pull-up to VDD_LED (the design that `Display.md` already describes). | Confirmed (topology); glow level: Measure |
| H-4 | High | Host Controller / R301, R302 | **The schematic has no I2C pull-ups.** R301/R302 (to +3.3V on SDA/SCL) have the value `dnp`. The firmware sets PA9/PA10 to `GPIO_NOPULL`. `Display.md` says 2.2k pull-ups are required, or the bus latches BUSY. | Netlist: R301/R302 value `dnp` on both builds. `Display_Board_BOM.csv`: "leave unfitted" (which is correct for display boards only). | Put `2k2` in the schematic for the host, and add a variant/DNP field so they are fitted on the host only. Check that the host board in use actually has them. | Confirmed (schematic); current board: Measure |
| H-5 | High | Root / J106, R101, U601/L601; system | **The power budget exceeds USB defaults, and nothing enforces a limit.** The worst case is ≈ 1.4–2.7 A for six boards at full brightness with Wi-Fi TX (see the load table). The CC pins are read by nobody (UCPD not enabled), so the firmware cannot tell a 500 mA port from a 3 A port. The INA180 channel saturates at ≈ 1.3 A (3.3 V ÷ 2.5 V/A), and the ADC is broken anyway (C-1). | Load table above. The `.ioc` does not enable UCPD1. `R101` 0.05 Ω 1206 dissipates 0.36 W at 2.7 A (typical 1206 rating 0.25 W; needs datasheet check for the fitted part). | Once C-1 is fixed, read the CC voltage (Rp level) and cap brightness in firmware, or limit brightness/segment count globally. Specify a 5 V/3 A supply. Next revision: consider a 2512 shunt or a lower gain (INA180A1), and see `USB_PD_Feasibility.md` for higher-voltage options. | Measure |
| M-1 | Medium | Power Supply / L601, U601 | **The inductor is far from the datasheet recommendation, has low saturation current, and has a footprint mismatch.** The fitted part is 33 µH `SRR6838A-330M` with Isat 1.02 A (per `Display_Board_Purchasing.md`). The TPS54202 table recommends 10 µH for 3.3 V out at 28 V in; at 5 V in, the Eq. 8 minimum is only ≈ 3–6 µH. The TPS54202 high-side current limit is 2.5–3.9 A, so on overload or short the inductor saturates long before the regulator limits. The host rail (LEDs + Wi-Fi ≈ 0.6–0.8 A) leaves little margin to 1.02 A. The ripple is only ≈ 60 mA, so the device runs in pulse-skip mode (300 mA peak threshold) up to fairly high loads. The schematic footprint is `L_Bourns_SRP7028A`, not the SRR6838A land pattern. | TPS54202 datasheet Table 7-2 (3.3 V: L = 10 µH, COUT = 44 µF, C6 = 56 pF), Eq. 8, I(LIM_HS) 2.5–3.9 A, I(SKIP) 300 mA. Schematic value `SRR6838-33u`, footprint `L_Bourns_SRP7028A_7.3x6.6mm`. | Use a 6.8–10 µH shielded inductor with Isat ≥ 3 A (≥ 2.5 A for TPS54202) on a matching footprint. Check the transient response with the 2×22 µF + 4.7 µF output (below the 44 µF in the table once DC bias is counted) and C608 47 pF. | Datasheet (confirmed values); stability: Measure |
| M-2 | Medium | Power Supply / U601 swap | **The TPS54302 → TPS54202 swap is pin-compatible**, but check the following. The TPS54202 is rated 2 A (the TPS54302 3 A), and runs at 500 kHz (not 400 kHz). EN floats to enable (internal pull-up, **confirmed**). VIN min is 4.5 V, UVLO rising is 4.2 V typ / **4.4 V max**, falling 3.7 V typ. A display board at the end of a daisy chain carrying ≈ 2 A can see VBUS near or below 4.4 V after cable and connector drops, and then fails to start. | TPS54202 datasheet: pinout GND, SW, VIN, FB, EN, BOOT = pins 1–6, which matches the netlist. "Float the EN pin to enable". Electrical table: UVLO, 0.596 V ±2.5% reference. The resulting VDD is 3.61–3.79 V. | Measure VBUS at the last board under full load. Keep the daisy chain short or use thicker wire. Next revision: consider feeding the chain from both ends, or a star connection. | Datasheet confirmed; margin: Measure |
| M-3 | Medium | Root / J105 AUX_POWER, VBUS_IN | **The AUX power header is wired straight onto USB VBUS.** Supplying J105 while USB is plugged in back-feeds the PC or charger. There is also no reverse-polarity protection (a 2-pin 2.54 mm header is easy to reverse, and TPS54x02 VIN abs min is −0.3 V), no fuse or polyfuse, and no VBUS TVS. | Netlist `VBUS_IN`: J105.2, J106 VBUS pins, R101, R304, U101.IN+. D101/D102 are only on D+/D−. | Add an ideal-diode OR, or a Schottky from each source, a polyfuse, and a VBUS TVS (for example SMF5.0A). | Confirmed |
| M-4 | Medium | Host Controller + all boards / PB8, J102/J104 pin 9, Q701 | **`LOW_POWER_ENABLE` is driven push-pull by every board.** The net is bussed to every board. The shared `main.c` configures PB8 as a push-pull output (driven low) in both firmware variants. As soon as the host drives it high, it shorts against five low outputs. Since 2026-09-24 the host drives it (switch 2 toggles low brightness), and the DisplayController firmware turns PB8 into an input at startup. | `main.c` `MX_GPIO_Init`: `LOW_POWER_EN_Pin` is `GPIO_MODE_OUTPUT_PP`, reset level low. There is one firmware tree with `FIRMWARE_VARIANT` HostController/DisplayController. | DisplayController variant: reconfigure PB8 as an input at startup (or leave it analog). Next revision: add a series resistor (1k) at each board's PB8. | Confirmed; firmware fixed 2026-09-24, series resistor still open |
| M-5 | Medium | Display + Light Sensor / LED_BRIGHTNESS, U503/U507/U511/U515, Q506–Q509, R504/R507/R510/R513 | **Analog brightness control has little headroom at both ends.** (1) The `LED_BRIGHTNESS` divider gives ≈ 1.17 V in bright light, 0.49 V with Q701 on, and ≈ 0.04 V in the dark (LDR 2 MΩ). At 40 mV, the op-amp offset (a few mV) and the ground offset along the daisy chain (LED return current in the cable, estimated tens of mV) are a large fraction of the signal. Remote boards and the four driver groups will then differ visibly, and remote boards may go dark. (2) The resulting SCT current is well below the SCT2024's regulated range of 5–45 mA. (3) If REXT sits near 1.2 V, the BC847 saturates at the bright end (V_E ≈ 1.17 V) and the loop loses control. (4) `R504`/`R507`/`R510`/`R513` are still "4k7 / 10k". (5) There is no filter capacitor on `LED_BRIGHTNESS`, which runs on the cable next to SCL/SDA. | Netlist: R701 4k7 + R703 (LDR 4k–2M) from 3.3V, R702 470k from 3.3V, R704 1k5 + R705 3k2 to GND (Q701 shorts R705). SCT2024 datasheet: recommended IOUT 5–30 mA at VDD 3.3 V. The REXT pin voltage is not stated. | Measure the REXT pin voltage and TP501–TP504 (emitters) at both light extremes. Add a minimum-brightness offset (for example a resistor from 3.3V to the bottom of the divider) and 100 nF at each op-amp input. Next revision: send brightness digitally over I2C and generate it locally (DAC or filtered PWM) on each board. Fix the R504 value. | Measure |
| M-6 | Medium | Display / U506, U508, U509 (FYM-7571AUHR-11) | **The matrix polarity needs checking.** The schematic drives `COL1..5` from the high-side P-FETs (anodes) and `ROW1..7` into SCT sinks (cathodes). TME lists FYM-7571AUHR-11 as "common electrode: cathode". If the custom `JPDisplays:FYM-7571A` symbol assigns anode/cathode wrongly, the matrix stays dark (reverse biased, no damage at 3.7 V). | TME parameters: cathode, VF 1.9–2.5 V, 20 mA. The Foryard datasheet could not be downloaded. The symbol pins are all type `input`, so no polarity is encoded. | Compare the symbol with the Foryard pinout drawing, or light one dot with a bench supply and a resistor. If it is already working on the host board, record that and close this item. | Datasheet |
| M-7 | Medium | ST67W611M1 / U201, U602 | **The 3.3 V LDO has limited margin for Wi-Fi TX bursts.** MCP1825S is a 500 mA LDO. The host 3.3 V load is roughly ST67 TX peak (≈ 300–400 mA, not verified) + ~45 mA logic. Dropout at 500 mA is ≈ 210 mV typ / 350 mV max (needs datasheet check) against 400 mV of headroom (3.7 → 3.3 V). The decoupling of 3×10 µF (one per VDD pin) matches the ST recommendation, and the supply range is 2.97–3.63 V. | ST wiki: one 10 µF per VDD pin. DB5452: input 2.97–3.63 V. Netlist: C201–C203 10u on +3.3V, no 100 nF. | Measure the 3.3 V droop during TX (scope on C201). If it droops below ≈ 3.1 V, raise VDD slightly (R602 18k → 3.9 V, which also worsens H-3) or use a 1 A LDO. | Measure / Datasheet |
| L-1 | Low | Host Controller / SW301, SW302 | **Button net names are swapped relative to the firmware.** In the schematic, `SWITCH_1` goes to pin 25 (PB13) and `SWITCH_2` to pin 24 (PB12). In `main.h`, `SWITCH_1` = PB12 and `SWITCH_2` = PB13. | Netlist vs `main.h`. | Rename in either place so they agree. | Confirmed |
| L-2 | Low | Host Controller / U301 | **The EEPROM part disagrees with the firmware.** The schematic has `AT24CS01-STUM` (1 Kbit, 128 B). The firmware driver is `Eeprom24AA04` (4 Kbit, and "answers on 0x50–0x57"). The SOT-23-5 pinouts match (SCL, GND, SDA, VCC, WP). WP is tied to GND, so writes are enabled. There is no local decoupling capacitor for U301. | Netlist: `U301.5(WP)` on GND. `User/Inc/Device/Eeprom24AA04.hpp`. | Put the fitted part in the schematic. The settings image is 256 B, so the 512-byte 24AA04 (the verified part) is required; a 128-byte AT24CS01 would not hold it. Add 100 nF at U301. | Confirmed / Datasheet |
| L-3 | Low | Display / U501, U505, U510, U513 | **Display power-up and reset behaviour.** OE/ has an internal 400k pull-up and LA/ a 400k pull-down, so the outputs are off and latched during MCU reset. But `MX_GPIO_Init` drives `SCT_ENABLE` (PB7) **low** before any data is latched, which can briefly show random latch contents. The P-FET gates float during reset (see H-3 (a)). | SCT2024 datasheet: R_UP on OE/ 400 kΩ, R_DOWN on LA/ 400 kΩ. `main.c`: PB7 reset level low. | Set the PB7 initial level high in CubeMX, and enable only after the first latch. | Confirmed |
| L-4 | Low | Display / U503…U515, MCP6006 | There is no decoupling on the op-amp supplies, and no base resistor between the op-amp output and the BC847. The loop (op-amp, emitter follower, 1k feedback) could oscillate with the REXT pin capacitance. | The netlist has no capacitors on the MCP6006 V+ pins. | Add 100 nF per op-amp. Look for oscillation at TP501–TP504. | Measure |
| L-5 | Low | Root / J106 shield, D101/D102 | The USB-C shield is marked no-connect. The D+/D− TVS parts are the generic `D_TVS` with no part number, so the capacitance for full-speed USB is not checked. VBUS has no TVS (see M-3). | Netlist: `J106.S1` has a no-connect flag. | Tie the shield to GND (directly, or through 1 MΩ ‖ 4.7 nF). Pick a low-capacitance USB TVS (for example USBLC6-2 or PESD5V0 with ≤ 1 pF). | Confirmed |
| L-6 | Low | Root / C602, display boards | **VBUS capacitance** is 10 µF on the host plus 10 µF on each display board (plus the buck soft-start load), about 60 µF in total. That exceeds the USB 2.0 10 µF inrush guideline. Type-C chargers usually tolerate it. | Netlist: C602 10u/35V on VBUS on every build. | Accept for a mains-supplied product, or add soft-start or load switches if hot-plug on a PC port is required. | Datasheet |
| L-7 | Low | Host Controller / Y301, R303, C303/C304 | **The crystal circuit is inconsistent across documents.** The schematic has Y301 at 24 MHz, R303 with value `x`, and 20 pF caps. The BOM doc lists an 8 MHz ABM3B with 0R. The `.ioc` sets `HSE_VALUE` = 8 MHz but runs from HSI, so HSE is unused. | Netlist, `Display_Board_BOM.csv`, `.ioc`. | Decide DNP or a value, and fix R303. If HSE is later used, the capacitors must match the crystal's CL. | Confirmed |
| L-8 | Low | Host Controller / PC14, PC15 | **The RTC runs from the LSI, which is not a precise clock.** PC14/PC15 (LSE) are unconnected. A dangling `OSC_32_OUT` label is left on the root sheet. The project already measures LSI drift and syncs from the network. | ERC: `label_dangling OSC_32_OUT`. Netlist: PC14/PC15 unconnected. | Next revision: add a 32.768 kHz crystal with the proper load caps on PC14/PC15. | Confirmed |
| L-9 | Low | PCB vs schematic | **The PCB is out of sync with the schematic.** The PCB has R304/R305 = **100k**, the schematic has 10k. The troubleshooting doc's 100k/1 µF (τ = 100 ms) matches the PCB. | `AstroWeather.kicad_pcb` footprint values. | Run *Update PCB from Schematic* before the next fab, and decide on the value. | Confirmed |
| L-10 | Low | Docs / Display.md | **`Display.md` describes hardware that is not there.** It says the slot FET is "switched on hard through a BC847 but turned off only by its gate pull-up resistor". In the schematic, the gate is driven directly from the MCU and has no pull-up. The BC847s are in the REXT current-set stages. The ghosting explanation and the 10 µs settle time are based on that wrong picture. | Netlist `DISPLAYx_EN` nets: only the MCU pin and the FET gate. | Update the doc. The residual "off" glow is H-3, not slow turn-off. | Confirmed |
| L-11 | Low | ERC | There are 21 ERC errors. All are expected or cosmetic: power flags missing on `+3.3V`/`VBUS`/`VDD`; ST67 XTAL32K pins without no-connect flags; U510 Out5–7 unused; the `SCT_SDO` hierarchical pin dangles (end of chain); `VDD`/`VLED` and `3.7V`/`+3.7V` alias pairs; many "net not bus member" warnings. | `kicad-cli sch erc` report. | Add PWR_FLAGs and no-connect markers so that real errors stand out. | Confirmed |
| I-1 | Info | Host Controller / UCPD1 | PA9/PA10 (I2C1) double as the UCPD1 dead-battery pins (DBCC1/DBCC2). The external 5.1k Rd on CC (R102/R103) is fine for a 5 V-only sink. Whether the internal dead-battery Rd briefly appears in parallel during reset, while the I2C pull-ups hold DBCC high, is unclear. See `USB_PD_Feasibility.md` for the PD blockers. | CubeMX DB: `UCPD1_DBCC1` on pin 29, `UCPD1_DBCC2` on pin 32. `.ioc`: `DisableDeadBatterySignals`. | No action for 5 V operation. | Datasheet |
| I-2 | Info | Light Sensor | The MCU never reads the light sensor. `LED_BRIGHTNESS` feeds only the op-amps and TP701. `LOW_POWER_ENABLE` (PB8 → Q701) is driven since 2026-09-24: `display low on|off` and switch 2 (`LowBrightness`). | grep of `User/`, and the netlist. | Optional: route `LED_BRIGHTNESS` to an ADC pin (after C-1) for logging or auto-dimming. | Confirmed |
| I-3 | Info | Host Controller | The unused pins PB0, PB4, PC14 and PC15 are left in their reset (analog) state, which is fine. PB0 is the natural home for the voltage sense (H-1). | Netlist. | — | Confirmed |
| I-4 | Info | `Controller.kicad_sch` | This is an orphan sheet (STM32G030F6P) that is not referenced by the root. | Root `Sheetfile` list. | Delete it or move it out of the project folder. | Confirmed |

## Per-subsystem notes

### STM32G0B1 (U302)

- **Supplies:** VDD/VDDA (pin 6) has 100 nF (C301) and 4.7 µF (C302), which matches ST's recommendation. VBAT (pin 4) is tied to +3.3V. **VREF+ (pin 5) is on GND (C-1)**; the prototype is reworked to VDD.
- **Reset and boot:** NRST has 100 nF (C305) and goes to SWD pin 5. BOOT0 shares PA14/SWCLK and is set by option bytes, so no strap is needed. SWD J103: 1 = 3.3V, 2 = SWCLK, 3 = GND, 4 = SWDIO, 5 = NRST, 6 = NC.
- **USB:** PA11/PA12 go directly to J106 D−/D+ (both rows), with TVS D101/D102 to GND. The pull-up is internal and series resistors are not needed for the G0 FS PHY. The firmware uses HSI48/CRS, so no crystal is needed.
- **5 V on MCU pins:** the only 5 V source is H-2 (PC7). I2C, `LOW_POWER_ENABLE` and `LED_BRIGHTNESS` on the cable are all 3.3 V domain.
- **Firmware pin map vs schematic:** everything matches (ST67 SPI1/USART2/CHIP_EN/RDY/CS/BOOT, SPI3 PB3/PB5, LA PB6, OE PB7, DISPLAY_1..5_EN = PD3/PA15/PD1/PD2/PD0, LED1 PC13, LED2 PB9, ADDR PB10/PB11/PB14, I2C PA9/PA10, USB), **except** CURRENT_SENSE (PB2 in firmware, PC6 in the schematic; H-1), the button names (L-1), and PC7, which the firmware sets to analog but cannot convert.
- **LED1 on PC13:** PC13 is in the backup domain (low drive, max about 2 MHz). With the 2k2 series resistor, the current is below 1 mA, which is fine.

### Display (U501/U505/U513 SCT2024, U510 SCT2167, Q501–Q509, U503…U515)

- **Segment mapping:** the segment-to-SCT-output mapping in the netlist matches the tables in `Display.md` for all four numeric displays, and matrix columns 1–21 map to U505 Out0–15 and U510 Out0–4. The chain order is U501 → U505 → U510 → U513, 56 bits in total, which matches the 7-byte frame.
- **Logic levels:** SCT VDD ≈ 3.2 V (3.3 V through 10 Ω). VIH = 0.7·VDD ≈ 2.25 V, so the 3.3 V MCU drive is fine. LA/ latches when low and is transparent when high, which matches the firmware sequence.
- **SPI clock:** `SCT_CLK` passes through R502 1k and C504 10 pF (plus four clock-input loads), giving τ ≈ 20–40 ns. At the current 1 MHz this is fine. Above about 4 MHz, check the edges (the SCT inputs are Schmitt triggers).
- **LED headroom:** VDD_LED 3.7 V − Si2333 drop (< 20 mV) − red LED Vf 1.8–2.5 V leaves 1.2–1.9 V at the SCT output. The dropout is about 0.55 V at 20 mA and less at 3–8 mA, so there is enough headroom. Power per SCT2024 at the highest estimated current is 16 × 7.5 mA × ≈ 1.8 V ≈ 0.22 W, and at 88 °C/W (SSOP24) that is about +19 °C, which is fine.
- **Duty cycle:** 5 slots at 250 Hz give a 1/5 duty and a 50 Hz frame. The peak current of 3.7–7.5 mA gives an average of 0.75–1.5 mA per segment, which may be dim in daylight. Displays are rated at 20 mA DC.
- **Slot switches:** see H-3.

### Power Supply (U601, U602)

- **VDD:** 3.70 V nominal, as computed from the divider. The input caps (10 µF/35 V + 100 nF) are adequate for 5 V in. BOOT has 100 nF (C601). EN floats (no UVLO divider), which is acceptable. See M-1 and M-2 for the inductor and the input range.
- **MCP1825S:** the pinout (1 VIN, 2 GND/tab, 3 VOUT) matches the netlist. Cin is 4.7 µF and Cout 1 µF, plus about 45 µF downstream. Dissipation is (3.7 − 3.3) × 0.45 A ≈ 0.18 W, which is fine. For the dropout margin, see M-7.

### USB-C input and current sense (root sheet)

- **CC:** 5.1k Rd on both CC lines (R102/R103), which is correct for a 5 V sink. The firmware does not read the advertised current (H-5).
- **INA180A2:** V+ is 3.3 V (C101 100 nF). The common-mode of 5 V is inside the −0.2…26 V range. The gain of 50 with the 50 mΩ shunt gives 2.5 V/A, so full scale is ≈ 1.3 A. The output feeds R305 (100k on the PCB) and C307 1 µF, τ = 100 ms. With a 160.5-cycle sample time and the 1 µF reservoir, the source impedance is acceptable.

### I2C bus (J101, J102/J104, U301)

- **Topology:** a daisy chain through the 10-pin connectors. Pins 1/3/5/7 are GND, 2 is SCL, 4 is SDA, 6/8 are VBUS, 9 is `LOW_POWER_ENABLE`, 10 is `LED_BRIGHTNESS`. There should be one pull-up pair, on the host only (H-4). With 2.2 kΩ and ≈ 300 pF, the rise time is ≈ 0.85 × R × C ≈ 0.56 µs, below the 1 µs Standard-mode limit.
- **Levels:** all 3.3 V, and PA9/PA10 are 5 V-tolerant anyway.
- **Ground noise:** the LED return current shares the GND pins. The resulting offset is small compared with the I2C VIL (≈ 1 V) but not small compared with `LED_BRIGHTNESS` (M-5).

### ST67W611M1 (U201)

- **Power:** VDD33 and both VDDIO pins are on +3.3V, with one 10 µF per VDD pin, as ST recommends.
- **CHIP_EN:** 33k pull-up with 100 nF (τ ≈ 3.3 ms), and PA0 push-pull. At power-up the module is enabled by the RC while the MCU is still in reset, then the firmware takes over. That is acceptable.
- **BOOT (PB1):** no external pull-down, so it floats during MCU reset. Check the module's internal default (datasheet).
- **SPI and UART:** SPI1 at 2 MHz and USART2 at 921600 baud, all at 3.3 V. SPI_RDY on PA4 has an internal pull-down.
- **XTAL32K pins:** left open (ERC flags them).
- **Antenna:** the module variant (-B PCB antenna or -U connector) is not recorded in the schematic. The antenna keep-out was not reviewed (PCB layout is out of scope).

### Light Sensor (Q701, R701–R705)

- **Divider:** described in M-5. Q701 (Si2374DS, N-channel) is driven from PB8 at 3.3 V, which is fine. Its gate has no pull-down, so it floats in reset. Only the brightness is affected.
- **Firmware use:** the sensor is not read; `Q701` is driven from PB8 by `display low` and switch 2 (I-2).

## Measure on the bench

1. **VREF+ (U302 pin 5):** confirm 0 V. After the rework, confirm 3.3 V, then apply 0.33 V, 1.0 V and 1.65 V to PB2 and check for raw ≈ 410, 1241 and 2048.
2. **Slot FET gate-source voltage when off:** measure V(VDD_LED) − V(gate) with the MCU driving high, and V(VDD_DIGx) on an "off" slot. In a dark room, check "off" digits and matrix rows for glow. Log VDD (expected 3.61–3.79 V).
3. **REXT pin voltage** on each SCT, and **TP501–TP504** (BC847 emitter) at bright, dark and Q701-on light levels. Check for BC847 saturation (Vce < 0.2 V) and op-amp oscillation (scope, AC-coupled).
4. **Segment current:** put a series ammeter (or a shunt) in one segment path at bright and dark light levels, to establish the real SCT gain.
5. **Rail ripple and droop:** VDD (3.7 V) and +3.3V at C201 during Wi-Fi TX bursts and slot switching. 20 MHz bandwidth, short ground spring.
6. **VBUS at every board** in the chain with all segments lit at full brightness (worst case). Check that it stays at or above 4.5 V at the last board (TPS54202 VIN min).
7. **Total VBUS current** at J106 at full load (use an external USB meter while the ADC is untrustworthy). Measure the L601 temperature and ripple current. Look for inductor saturation (peaked triangle on the SW-node current) at maximum load.
8. **I2C rise time** on SCL/SDA at the host and at the farthest board (target < 1 µs at 100 kHz). Confirm that pull-ups are fitted on the host only.
9. **PC7 voltage** with USB connected (expected ≈ 5 V through R304). Remove R304 until the divider is added.
10. **`LOW_POWER_ENABLE`:** check its level with all boards connected, and confirm that no board fights the host.
11. **LED_BRIGHTNESS at each remote board** relative to that board's GND, in darkness with the display fully lit, to size the ground offset (M-5).
12. **Matrix polarity:** if it has not been seen lit yet, bias one dot with a bench supply through 1 kΩ.
