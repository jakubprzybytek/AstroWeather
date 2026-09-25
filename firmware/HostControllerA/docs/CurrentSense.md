# Current Sense

## Overview

The HostController measures the current flowing from `VBUS_IN` (USB or the
auxiliary power input) into `VBUS`, which supplies the board and the external
display boards, through a shunt and an INA180A2 current-sense amplifier read by
ADC1. `CurrentSenseTask` samples it ten times a
second, together with the MCU's internal temperature sensor and `VREFINT`, and
by default shows the current in mA on numeric display 2 of the local board.
`adc log on` also logs every sample.

Only the HostController firmware has the task; the DisplayController project
has no ADC configured.

The measurement works on the prototype host board, which has a hand rework:
`VREF+` (U302 pin 5) is tied to GND in the schematic and PCB, and has been
rewired to VDD on the board. Boards built from the current design files need
the same rework until the schematic and PCB are fixed (issue C-1 in
[Hardware_Review.md](../../../KiCad/Hardware_Review.md)). See
[Troubleshooting history](#troubleshooting-history).

## Signal Chain

| Stage | Value |
| --- | --- |
| Shunt | 50 mΩ, `R101`, between `VBUS_IN` and `VBUS` |
| Amplifier | INA180A2, `U101`, gain 50 V/V |
| Scale at the amplifier output | 50 mΩ × 50 = **2.5 V/A** |
| Filter | 100 kΩ series, 1 µF to ground: τ = 100 ms, corner ≈ 1.6 Hz |
| ADC input | `PB2`, `ADC1_IN10`, label `CURRENT_SENSE` |
| Reference | VDDA, nominally 3.3 V, measured each sample from `VREFINT` |

Full scale at VDDA = 3.3 V is 3.3 / 2.5 = **1.32 A**, about 0.32 mA per count.
The amplifier's output headroom and offset make the usable range somewhat
smaller.

The filter dominates the response: after a load step the reading settles over
several hundred milliseconds. The 10 Hz sampling is for monitoring, not for
protection or transient detection.

### PB2 or PC6: hardware item

The firmware reads `PB2` (pin 21). The KiCad schematic routes
`CURRENT_SENSE_FLTR` to `PC6` (pin 30) instead; see
[USB_PD_Feasibility.md](USB_PD_Feasibility.md). During bring-up `PC6` was
found physically connected to `PB2` on the board in use, so both pins see the
filtered signal. `PC6` (and `PC7`, `VOLTAGE_SENS_FLTR`) are configured as
analog inputs with no pull, which keeps them high impedance.

Confirm which revision a board is before relying on this, and resolve the
schematic/firmware disagreement in the next hardware revision.

## ADC Configuration

As generated into `MX_ADC1_Init()` in `Core/Src/main.c` from
`HostControllerA.ioc`:

| Setting | Value |
| --- | --- |
| Clock | `ADC_CLOCK_SYNC_PCLK_DIV2`: 8 MHz from the 16 MHz HSI |
| Resolution, alignment | 12-bit, right |
| Mode | Scan, 3 regular conversions, single (not continuous), software start |
| Rank 1 | `ADC_CHANNEL_10`, `PB2`, the current |
| Rank 2 | `ADC_CHANNEL_TEMPSENSOR` |
| Rank 3 | `ADC_CHANNEL_VREFINT` |
| Sample time | 160.5 cycles for all three (`SamplingTimeCommon1`) |
| Oversampling | **Enabled**: 16×, right shift 4, so results stay 12-bit |
| End of conversion | `ADC_EOC_SINGLE_CONV`, overrun keeps old data |
| DMA | `DMA1_Channel3`, peripheral to memory, half-word both sides, normal mode, low priority |

The long sample time suits the 100 kΩ source impedance and also meets the
temperature sensor's minimum. One conversion takes 173 ADC cycles, about
22 µs, so a full oversampled three-channel sequence takes about 1 ms.

Changes belong in the `.ioc`, followed by regeneration; see
[CubeMXCompliance.md](CubeMXCompliance.md).

## Software

| File | Responsibility |
| --- | --- |
| `User/Inc/Sensors/CurrentSenseTask.hpp`, `User/Src/Sensors/CurrentSenseTask.cpp` | The sampling task, and the HAL ADC complete and error callbacks. |
| `User/Inc/Sensors/CurrentSenseConversion.hpp` | Pure conversions: ADC counts to mA, VDDA from `VREFINT`, the temperature, and the value shown. Tested by `tests/CurrentSenseConversionTests.cpp`. |
| `User/Src/Console/AdcCommand.cpp` | `adc log` and `adc display`, saved to the EEPROM. |
| `User/Src/AstroWeather.cpp` | Applies the saved flags, sets the display and starts the task. |

`CurrentSenseTask` is a `Task<2048>` at `osPriorityBelowNormal`.

### Sampling

1. On start the task calibrates the ADC once with
   `HAL_ADCEx_Calibration_Start()`, logging `CurrentSense ADC calibration
   failed` if that fails.
2. Every 100 ms, paced with `osDelayUntil()` on an absolute tick so that
   conversion and logging time do not add up, it:
   - starts the three-channel sequence with `HAL_ADC_Start_DMA()` into a
     three-entry `uint16_t` buffer;
   - waits up to 10 ms for the thread flag set by `HAL_ADC_ConvCpltCallback()`
     or `HAL_ADC_ErrorCallback()`;
   - stops with `HAL_ADC_Stop_DMA()`.
3. A start failure, timeout or ADC error logs
   `CurrentSense ADC conversion failed` for that sample.

### Conversion

`VDDA` comes from the `VREFINT` reading with
`CurrentSense::vddaMilliVolts()`, `VREFINT_CAL × 3000 / VREFINT_DATA`, the
arithmetic of `__HAL_ADC_CALC_VREFANALOG_VOLTAGE()`. The task reads the factory
`VREFINT_CAL` and passes it in. A zero `VREFINT` reading falls back to 3300 mV.

The current is then `CurrentSense::rawToMilliAmps(raw, vddaMilliVolts)`:

```text
current_mA = raw × VDDA_mV × 1000 / (4095 × 2500)
```

in 64-bit integer arithmetic, truncated to whole mA. An earlier 32-bit version
overflowed. No zero-current offset is subtracted, so the INA180's input offset
shows as a small reading at no load.

The temperature, in whole °C, comes from `CurrentSense::temperatureCelsius()`,
the arithmetic of `__HAL_ADC_CALC_TEMPERATURE()`: the reading rescaled to 3.0 V
with the measured VDDA, then interpolated between the factory `TS_CAL1` (30 °C)
and `TS_CAL2` (130 °C), which the task reads and passes in. `static_assert`s in
the task keep the header's calibration constants equal to the device header's. It and VDDA are only logged;
nothing else uses them yet. [RTC.md](RTC.md#trimming) notes that the
temperature could explain the LSI drift if the two were logged together.

## Display

While `adc display` is on, the default, each valid sample is written to
**local numeric display 2** as an integer in mA with `setValue(int16_t)`, and
the local board alone is refreshed with `Display::submitLocal()`, which sends
nothing over I2C.

A value above 9999 shows the numeric display's error pattern, an underscore on
each digit: `CurrentSense::displayMilliAmps()` turns it into a value the display
rejects. With a 1.32 A full scale that cannot happen in practice.

`adc display off` stops the writes but does not blank the display: it keeps the
last reading until something else writes numeric 2. The astro refresh does, with
block 0's maximum temperature; see
[AstroRefresh.md](AstroRefresh.md#local-numerics-2-and-3). With `adc display on`,
that temperature is overwritten within 100 ms.

## Console Commands

| Command | Effect |
| --- | --- |
| `adc log on\|off` | Log every sample at debug level. Saved. Default off. |
| `adc display on\|off` | Show the current on local numeric 2. Saved. Default on. |

Both are saved at once in the `AdcFlags` settings record (tag `0x01`: bit 0
logging, bit 1 display) and applied at the next boot before the task starts;
see [Settings.md](Settings.md). Each logged sample has the form:

```text
CurrentSense raw=<counts> current_mA=<mA> temp_raw=<counts> temp_C=<C> vref_raw=<counts> vdda_mV=<mV>
```

Ten lines a second is a lot of log traffic; leave `adc log` off unless
diagnosing. See [Console.md](Console.md) for the replies.

## Tests

`tests/CurrentSenseConversionTests.cpp` checks `rawToMilliAmps()` for raw 0,
255, 511 and 4095 at the nominal 3.3 V, 2048 at 3.0 V and 4095 at 3.6 V VDDA;
`vddaMilliVolts()` at typical readings and the zero fallback;
`temperatureCelsius()` at both calibration points, between and below them, at
3.3 V VDDA and with equal calibration points; and `displayMilliAmps()` either
side of 9999.

Not covered: the task, the ADC and DMA configuration, reading the factory
calibration values, and the display output. These have been checked on hardware only,
on the reworked prototype.

## Troubleshooting History

This records the investigation into readings that did not match the pin
voltage, from the former `ADC_Current_Monitor_Troubleshooting.md`
([archive](archive/ADC_Current_Monitor_Troubleshooting.md)). That document
recorded **no resolution**. The configuration it was debugging was a single
channel read by polling; the firmware has since moved to the three-channel DMA
scan above, **with oversampling enabled**, which is the opposite of what its
resolution path recommended while diagnosing.

**Resolution:** the 2026-09-23 hardware review found `VREF+` (U302 pin 5) tied
to GND in both the schematic and the PCB, so the ADC had no reference and every
result, VREFINT and temperature included, was meaningless. Rewiring pin 5 to
VDD on the prototype fixed it; current sensing now works with the shipped
three-channel, oversampled configuration. The schematic and PCB still carry
the fault (issue C-1 in
[Hardware_Review.md](../../../KiCad/Hardware_Review.md)). The rest of this
section is kept for reference.

### Symptom

An oscilloscope showed 20–30 mV at `PB2`, which should read about 25–37 counts,
or 8–12 mA. The firmware reported instead:

- **Oversampling off**: only `raw=255` (0x0FF, 82 mA) and `raw=511` (0x1FF,
  164 mA), which would be 205 mV and 412 mV.
- **16× oversampling, shift 4**: values such as 511, 495, 447, 431, 367, 303,
  271 and 319, many of the form 16 × N − 1 (511 = 16 × 32 − 1,
  495 = 16 × 31 − 1, 447 = 16 × 28 − 1).

A repeated low nibble of `0xF` is not what ordinary quantisation produces. The
exact 0x0FF and 0x1FF values are suspicious but not proof of misalignment: they
are also valid readings of a real 205 mV or 412 mV. Left alignment would give
about 4080 for 205 mV, so it does not explain them either.

### Ruled out

- The 32-bit overflow in the conversion, since fixed with 64-bit arithmetic.
- Pin and channel setup: `PB2` analog with no pull, `ADC1_IN10` at rank 1,
  12-bit right-aligned, 160.5-cycle sample time, 100 ms cadence.

### Still possible

1. The scope and the ADC were not measuring the same node or ground.
2. The ADC's runtime registers differ from the generated source or the flashed
   image.
3. The result is being read in the wrong format.
4. Something else on the `PB2`/`PC6` net affects the signal.
5. The signal moves faster than the scope capture or the sampling reveals.
6. Oversampling or its shift is not applied as configured.

### Register check

Capture the data, status and configuration registers at conversion time and
log them in hex:

```cpp
const uint32_t dr = ADC1->DR;
const uint32_t isr = ADC1->ISR;
const uint32_t cfgr1 = ADC1->CFGR1;
const uint32_t cfgr2 = ADC1->CFGR2;
```

Reading `DR` clears the end-of-conversion flag, so read it once and use that
value as the sample. With DMA the data register has already been read by the
DMA, so for this check use the DMA buffer as the sample and read the
configuration registers only. Expect:

| Field | Expected now |
| --- | --- |
| `CFGR1.RES` | 12-bit |
| `CFGR1.ALIGN` | right |
| `CHSELR` | channel 10, the temperature sensor and `VREFINT` |
| `CFGR2.OVSE` | 1, oversampling on |
| `CFGR2.OVSR` | 16× |
| `CFGR2.OVSS` | shift 4 |

### Known-voltage test

Apply a stable voltage directly to `PB2` and compare. Never exceed VDDA.

| Voltage | Expected raw at 3.3 V |
| --- | --- |
| 0.000 V | ≈ 0 |
| 0.330 V | ≈ 410 |
| 1.000 V | ≈ 1241 |
| 1.650 V | ≈ 2048 |

If this passes, the problem is in the analog path; if not, in the ADC setup.

### Hardware checks

Measure, against MCU ground, both the INA180 output and the `PB2`/`PC6` net at
the MCU pin, preferably with a scope trace of each; `PB2` should be much slower
than the amplifier output. The network should be:

```text
INA180 OUT ---- 100 kΩ ---- PB2
                            |
                           1 µF
                            |
                           GND
```

Confirm that:

- the capacitor is fitted, is really 1 µF, and is on the `PB2` side of the
  resistor;
- `PC6` has no external pull, digital output or other connection;
- the INA180 and MCU grounds are common;
- INA180 `REF` is connected for unidirectional measurement;
- the INA180 output stays within its supply and the ADC input limits.

### Suggested order

1. Run the known-voltage test on the shipped configuration.
2. Capture the registers.
3. If both are right, investigate the INA180 output, the RC network and the
   `PB2`/`PC6` net.
4. If the known-voltage test fails with oversampling, repeat it with
   oversampling off to separate the two.
5. Then measure the zero-current offset against a trusted meter, and decide
   whether to subtract it.
