# Current Sense

## Overview

The HostController measures the current flowing from `VBUS_IN` (USB or the
auxiliary power input) into `VBUS`, which supplies the board and the external
display boards, through a shunt and an INA180A2 current-sense amplifier read by
ADC1. `CurrentSenseTask` samples it ten times a
second, together with the MCU's internal temperature sensor and `VREFINT`, and
by default shows the current in mA on numeric display 2 of the local board.
`adc log on` also logs every sample. With both `adc display` and `adc log`
off, nothing uses a reading, so the task stops sampling and the ADC stays idle.

Only the HostController firmware has the task; the DisplayController project
has no ADC configured.

The measurement works on the prototype host board, which has a hand rework:
`VREF+` (U302 pin 5) is tied to GND in the schematic and PCB, and has been
rewired to VDD on the board. Boards built from the current design files need
the same rework until the schematic and PCB are fixed (issue C-1 in
[Hardware_Review.md](../../../KiCad/Docs/Hardware_Review.md)). Without it the
ADC has no reference and every result, VREFINT and temperature included, is
meaningless. The investigation that found this is archived in
[ADC_Current_Monitor_Troubleshooting.md](archive/ADC_Current_Monitor_Troubleshooting.md).

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
`CURRENT_SENSE_FLTR` to `PC6` (pin 30) instead (issue H-1 in
[Hardware_Review.md](../../../KiCad/Docs/Hardware_Review.md)). On the prototype
host board `PC6` is connected to `PB2`, so both pins see the filtered
signal. `PC6` (and `PC7`, `VOLTAGE_SENS_FLTR`) are configured as
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
[CubeMXCompliance.md](archive/CubeMX_Compliance_Migration.md).

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
4. While `adc display` and `adc log` are both off, the task skips all of this
   and blocks on a thread flag instead, so there are no conversions, no DMA
   interrupts and no wake-ups. Switching either on (`setDisplayEnabled()`,
   `setLoggingEnabled()`, from the console or at boot) sets the flag, and the
   task takes a reading at once and resumes the 100 ms cadence from there.
   Switching the display off leaves the last value on numeric 2.

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
