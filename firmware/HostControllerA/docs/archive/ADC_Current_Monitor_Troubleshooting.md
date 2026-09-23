# ADC Current Monitor Troubleshooting Summary

## Problem

The current monitor reads `ADC1_IN10` on `PB2`. The input is connected to an INA180A2 current-sense amplifier with:

- INA180 gain: 50 V/V
- Shunt resistor: 50 mOhm
- ADC input filter: 100 kOhm series resistor and 1 uF capacitor to ground
- Intended sample cadence: one reading every 100 ms
- Nominal ADC reference: VDDA = 3.3 V

The oscilloscope measures approximately 20-30 mV at the PB2 ADC node, but firmware reports unexpected ADC values.

## Expected result

The INA180 output relationship is:

```text
Vout = I * 0.05 ohm * 50
Vout = I * 2.5 V/A
I    = Vout / 2.5 V/A
```

For a 12-bit, right-aligned ADC using a 3.3 V reference:

```text
ADC raw = Vpb2 / 3.3 V * 4095
```

Therefore, a measured PB2 voltage of 20-30 mV should produce approximately:

```text
20 mV -> raw 25 -> current 8 mA
30 mV -> raw 37 -> current 12 mA
```

The nominal conversion used by the application is:

```text
current_mA = raw * 3300 / (4095 * 2.5)
```

The implementation uses 64-bit intermediate arithmetic to prevent overflow.

## Observed behavior

### Oversampling disabled

The firmware repeatedly reports only:

```text
raw=255  (0x0FF), current_mA=82
raw=511  (0x1FF), current_mA=164
```

For a normal 12-bit right-aligned conversion, those values correspond to approximately:

```text
raw 255 -> 205 mV -> 82 mA
raw 511 -> 412 mV -> 164 mA
```

They do not correspond to the independently measured 20-30 mV at PB2.

### 16x oversampling with four-bit right shift

The firmware reports values such as:

```text
511, 495, 447, 431, 367, 303, 271, 319
```

Many values have the form:

```text
16 * N - 1
```

For example:

```text
511 = 16 * 32 - 1
495 = 16 * 31 - 1
447 = 16 * 28 - 1
```

The current generated ADC configuration at that point was:

```c
hadc1.Init.Resolution = ADC_RESOLUTION_12B;
hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
hadc1.Init.OversamplingMode = ENABLE;
hadc1.Init.Oversampling.Ratio = ADC_OVERSAMPLING_RATIO_16;
hadc1.Init.Oversampling.RightBitShift = ADC_RIGHTBITSHIFT_4;
```

The repeated lower nibble `0xF` is not expected from ordinary analog quantization. It suggests that the value format, applied shift, runtime configuration, or data-register interpretation must be verified.

## Confirmed project configuration

- `PB2` is configured as `ADC1_IN10`.
- The GPIO is configured in analog mode with no pull resistor.
- ADC resolution is configured as 12-bit.
- ADC data alignment is configured as right-aligned.
- ADC rank 1 contains channel 10.
- The sample time is configured as 160.5 ADC cycles, appropriate for the high source impedance.
- The application starts one software-triggered conversion and reads it with `HAL_ADC_GetValue()`.
- The application task schedules readings every 100 ms using `osDelayUntil()`.
- PC6 is physically connected to PB2 and is configured as analog/no-pull. This should be high impedance, but any other circuitry on the PC6 net can still affect the measurement.

## Current interpretation

The issue is not the 32-bit arithmetic overflow that was previously present. That overflow has been fixed by promoting the calculation to 64-bit before multiplication.

The issue is also not explained by normal ADC left alignment:

```text
12-bit right-aligned 0.205 V -> approximately 255
12-bit left-aligned  0.205 V -> approximately 4080
```

The fact that the scope sees 20-30 mV while the ADC reports values representing 205-412 mV means at least one of the following remains possible:

1. The oscilloscope probe and ADC conversion are not observing the same electrical node or ground reference.
2. The ADC runtime registers differ from the generated source or flashed image.
3. The ADC data is being interpreted with the wrong result format.
4. Another circuit connected to the PB2/PC6 net is influencing the signal.
5. The signal changes faster than the scope measurement or the ADC sampling interval reveals.
6. The oversampling setting or right shift is not applied as expected at runtime.

The exact `0xFF` and `0x1FF` values are suspicious, but they are not by themselves proof of bit misalignment. They are valid numeric values for a normal 12-bit ADC and could result from a real 205 mV or 412 mV signal.

## Required diagnostic

The next firmware diagnostic should capture the ADC data register, status register, and configuration registers at the time of conversion:

```cpp
const uint32_t raw = HAL_ADC_GetValue(&hadc1);
const uint32_t dr = ADC1->DR;
const uint32_t isr = ADC1->ISR;
const uint32_t cfgr1 = ADC1->CFGR1;
const uint32_t cfgr2 = ADC1->CFGR2;
```

Log the values in hexadecimal. Reading `ADC1->DR` after `HAL_ADC_GetValue()` may clear the end-of-conversion flag, so for a precise register capture read the data register once and use that value as the sample, or capture the register before the HAL call during a diagnostic build.

Verify these runtime fields:

```text
CFGR1.RES  = 12-bit setting
CFGR1.ALIGN = right alignment
CFGR2.OVSE = disabled or enabled as intended
CFGR2.OVSR = 16x only when oversampling is enabled
CFGR2.OVSS = 4 only when a four-bit shift is intended
```

The most useful data-register test is to compare a stable, known voltage against the result:

```text
0.000 V -> approximately raw 0
0.330 V -> approximately raw 410
1.000 V -> approximately raw 1241
1.650 V -> approximately raw 2048
```

Do not apply a voltage above VDDA to PB2.

## Hardware checks

Measure both points relative to the STM32 ground:

```text
INA180 OUT
PB2 / PC6 net, directly at the MCU pin
```

The PB2 network should be:

```text
INA180 OUT ---- 100 kOhm ---- PB2
                              |
                             1 uF
                              |
                             GND
```

Confirm:

- The capacitor is populated and really 1 uF.
- The capacitor is connected to the PB2 side of the resistor.
- PC6 has no external pull-up, pull-down, digital output, or peripheral connection.
- INA180 ground and STM32 ground are common.
- INA180 REF is connected for the intended unidirectional measurement.
- The INA180 output remains within its supply and ADC input limits.

An oscilloscope trace at both INA180 OUT and PB2 is preferable. The 100 kOhm / 1 uF filter has a 100 ms time constant and an approximate 1.59 Hz cutoff, so PB2 should be much slower than the unfiltered amplifier output.

## Resolution path

1. Keep oversampling disabled while diagnosing the basic ADC path.
2. Apply a stable known voltage directly to PB2 and verify the four expected ADC results above.
3. Capture runtime `CFGR1`, `CFGR2`, `ISR`, and `DR` values.
4. If the known-voltage test works, investigate the INA180 output, the RC network, and the PB2/PC6 PCB net.
5. Re-enable oversampling only after the basic 12-bit result is correct.
6. With 16x oversampling, confirm the returned value is in the expected range after the configured four-bit shift before applying the normal current conversion.

## Test coverage

The pure conversion function is implemented in:

```text
User/Inc/Sensors/CurrentSenseConversion.hpp
```

and tested in:

```text
tests/CurrentSenseConversionTests.cpp
```

The test covers raw values 0, 255, 511, 4095, and a custom 3.0 V VDDA case. The native conversion test passes, and the firmware build passes.
