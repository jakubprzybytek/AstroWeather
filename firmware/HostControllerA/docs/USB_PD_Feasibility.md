# USB Power Delivery Feasibility

## Goal

Negotiate a VBUS voltage above 5 V through USB Power Delivery (PD) so the host controller can deliver more power to the external boards connected on `J102`/`J104`.

## Summary

The MCU and the power stage can support a PD sink. Firmware alone is not enough. The current board needs at least one hardware change, and the target voltage must be one that every external board can tolerate.

## Sources and caveats

- Hardware findings come from the KiCad project in `KiCad/` (netlist exported with KiCad 9 `kicad-cli`) and from `HostControllerA.ioc`.
- The schematic and firmware do not fully agree. The schematic routes `CURRENT_SENSE_FLTR` to `PC6` (pin 30), while the firmware reads `ADC1_IN10` on `PB2` (pin 21). Confirm that the board in use matches the schematic revision before acting on these findings.

## Relevant nets

| Net | Connections |
|---|---|
| `VBUS_IN` | `J106` VBUS pins, `J105` (AUX_POWER), `R101` shunt, `R304`, `U101` IN+ |
| `VBUS` | `R101` shunt, `U101` IN-, `U601` VIN, `C602`, `C603`, `J102`/`J104` pins 6 and 8 |
| `UCPD_CC1` | `J106.A5`, `R103` 5.1 kOhm to GND, `U302.28` (`PA8`, `UCPD1_CC1`) |
| `UCPD_CC2` | `J106.B5`, `R102` 5.1 kOhm to GND, `U302.27` (`PB15`, `UCPD1_CC2`) |
| `VOLTAGE_SENS_FLTR` | `R304` from `VBUS_IN`, `C306` to GND, `U302.31` (`PC7`) |

## What already supports PD

| Item | Status |
|---|---|
| MCU `STM32G0B1CET6` | Has the UCPD1/UCPD2 PD controllers. |
| CC routing | CC1/CC2 reach `PA8`/`PB15` (UCPD1). Both pins are unused in the `.ioc`. |
| Interrupt | `USB_UCPD1_2_IRQn` is already enabled. The vector is shared with USB, so the handler must serve both. |
| Buck `U601` TPS54302 | 4.5-28 V input. The output is about 3.7 V (`R601` 100k / `R602` 19k2) regardless of input voltage. |
| Input caps `C602`/`C603` | Rated 35 V. |
| Current sense `U101` INA180A2 | Common-mode range up to 26 V. 20 V works with little margin. |
| Flash, RAM, FreeRTOS | ST's USBPD middleware for a sink-only device fits easily. |

## Blockers

### 1. Fixed Rd resistors on CC

`R102`/`R103` are fixed 5.1 kOhm pull-downs. When UCPD runs as a sink it applies its own internal Rd on the active CC line. In parallel with the external resistor that gives about 2.55 kOhm, which is outside the Type-C Rd range. A source may then misdetect the attachment or fail to exchange PD messages.

The resistors cannot simply be removed. On the STM32G0, the dead-battery pull-downs that let an unpowered sink receive 5 V come from the separate DBCC pins, which must be wired to CC externally. For UCPD1 those pins are `PA9`/`PA10`, which this board uses for I2C1. The `.ioc` also disables the dead-battery signals (`VP_SYS_VS_DBSignals`). Without `R102`/`R103`, a PD charger would never enable VBUS and the MCU would never start.

Possible fixes:

- Make the external Rd switchable, for example with a small FET that is on by default and turned off by the MCU once UCPD takes over.
- Use ST's TCPP01-M12, which provides a dead-battery Rd and releases it when the MCU is running.

### 2. No over-voltage protection on CC

In the Type-C connector, CC sits next to VBUS. At 20 V, a bent plug or a poor cable can short VBUS to CC and put 20 V on `PA8`/`PB15`. ST recommends the TCPP01-M12 for CC and VBUS over-voltage protection in UCPD sink designs.

### 3. External boards receive raw VBUS

`J102`/`J104` pins 6 and 8 connect directly to `VBUS`, and the AUX_POWER header `J105` connects to `VBUS_IN`. Whatever voltage is negotiated reaches every external board. Each external board must be checked against the chosen voltage. For example, the Si2333DDS P-FETs used in the display circuits allow only ±8 V gate-source.

### 4. VBUS voltage sense has no divider

`R304` (10 kOhm) connects `VBUS_IN` directly to `PC7` with only `C306` to ground and no pull-down resistor. This is already questionable at 5 V, because a pin in analog mode is not 5 V tolerant. At 9-20 V it injects about 0.5-1.6 mA into the pin. Add a resistor to ground to form a proper divider.

## Other checks at higher power

- Shunt `R101` (0.05 Ohm, 1206) dissipates 0.45 W at 3 A, above a typical 1206 rating.
- With gain 50 on a 3.3 V supply, the INA180A2 output saturates at about 1.3 A. This is acceptable at higher voltages, where the same power needs less current.
- Without an e-marked cable the maximum current is 3 A. Realistic contracts are 9 V × 3 A = 27 W, 15 V × 3 A = 45 W, or 20 V × 3 A = 60 W, compared with at most 15 W at 5 V today.
- The external boards' capacitors add to the total VBUS capacitance seen by the source. Check the total against the PD sink limits.
- Confirm the current rating of the `J106` connector (GCT USB4216).

## Options

### A. Rework the current board and implement PD in firmware

- Make `R102`/`R103` switchable, or add a TCPP01-M12 as a bodge.
- Add the `PC7` divider resistor.
- Enable UCPD1 on `PA8`/`PB15` in CubeMX and add ST's USBPD middleware as a sink that requests one fixed voltage.

This is the cheapest way to try PD. CC stays unprotected unless the TCPP01-M12 is added.

### B. Next board revision with MCU-driven PD (recommended for firmware control)

Place a TCPP01-M12 between `J106` and `PA8`/`PB15`, following ST's sink reference design (AN5225), and fix the `PC7` divider.

### C. Next board revision with a standalone PD controller

Use a dedicated PD controller such as the STUSB4500 (configured over the existing I2C bus) or the CH224K (voltage selected by resistors). No firmware PD stack is needed. The CC lines go to the controller instead of the MCU.

## Next steps

1. Choose one target voltage that every external board can tolerate.
2. Choose an option above.
3. For option A or B, add the CubeMX UCPD1 configuration and a minimal USBPD sink that requests the target voltage and falls back to 5 V.
