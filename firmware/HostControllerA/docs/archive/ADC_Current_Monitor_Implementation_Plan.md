# ADC Current Monitor Implementation Plan

## Goal

Read the INA180A2 current-sense output on `ADC1_IN10` / `PB2` every 100 ms and publish the measured current through `DebugService`.

The application implementation belongs under:

- `User/Inc/Sensors/`
- `User/Src/Sensors/`

No ADC configuration should be hand-edited in generated C files. Hardware configuration changes belong in `HostControllerA.ioc`, followed by CubeMX regeneration.

## Existing hardware and signal model

- MCU: STM32G0B1
- ADC input: `PB2` / `ADC1_IN10`
- Current amplifier: INA180A2
- Amplifier gain: 50 V/V
- Shunt: 50 mOhm
- Analog filter at ADC input: 100 kOhm and 1 uF
- Nominal ADC reference: `VDDA`, assumed to be 3.3 V unless measured or compensated with `VREFINT`

The current conversion is:

```text
Vshunt = I * 0.05 ohm
Vout   = Vshunt * 50 = I * 2.5 V/A
I      = Vout / 2.5 V/A
```

For a 12-bit ADC and nominal 3.3 V VDDA:

```text
current_mA = adc_raw * 3300 / (4095 * 2.5)
```

The zero-current offset must be considered because INA180 input offset and board offsets can produce a nonzero ADC reading at zero load.

## Phase 1: Verify CubeMX configuration

In STM32CubeMX, verify or configure `ADC1` as follows:

- Resolution: 12 bits
- Data alignment: right
- Scan mode: disabled
- Regular conversions: 1
- Rank 1 channel: `ADC_CHANNEL_10`
- Continuous conversion: disabled
- External trigger: software start
- EOC: single conversion
- DMA: disabled for the initial implementation
- GPIO `PB2`: analog mode, no pull
- Sampling time: use a long setting, initially `160.5 cycles`, because the external 100 kOhm source impedance is high
- ADC clock: retain the existing valid ADC clock configuration

The 100 kOhm / 1 uF filter has:

```text
tau = R * C = 100 ms
fc  = 1 / (2*pi*R*C) approximately 1.59 Hz
```

This filter dominates signal response. A 100 ms task cadence therefore samples a deliberately slow signal and does not provide instantaneous load-current reporting.

After regeneration, inspect the generated code and confirm:

- `PB2` is initialized as analog
- ADC channel 10 is rank 1
- the selected sampling time is present
- ADC initialization still occurs before the scheduler starts

## Phase 2: Add the sensor task interface

Create `User/Inc/Sensors/CurrentSenseTask.hpp`.

Recommended responsibilities:

- Define `CurrentSenseTask`, derived from the project `Task<>` wrapper.
- Provide a singleton `instance()` or a static object, matching existing task patterns.
- Provide `start()` through the inherited task API.
- Keep the latest raw ADC sample and converted current in task-owned state only if another module will later consume them.
- Use a stack size appropriate for `DebugService::logf()` calls. Start with `Task<768>` or larger and verify stack headroom during testing.

Suggested public surface:

```cpp
class CurrentSenseTask : public Task<768>
{
public:
    static CurrentSenseTask& instance();

protected:
    void run() override;

private:
    CurrentSenseTask();
    uint32_t readRaw();
};
```

The task should not expose the generated `hadc1` handle through a new global API unless required. The handle is currently defined in generated `main.c`; application code may access it through an `extern "C"` declaration, but this coupling should be kept in the sensor implementation, not in its public header.

## Phase 3: Implement ADC readout

Create `User/Src/Sensors/CurrentSenseTask.cpp`.

Initialization sequence:

1. Keep `MX_ADC1_Init()` in generated `main.c`.
2. After `MX_ADC1_Init()` and before normal conversions, perform ADC calibration once using `HAL_ADCEx_Calibration_Start(&hadc1)`.
3. Start the task only after ADC initialization and RTOS initialization are complete.

Single-conversion helper behavior:

1. Call `HAL_ADC_Start(&hadc1)`.
2. Call `HAL_ADC_PollForConversion(&hadc1, timeout)` with a short timeout.
3. Read `HAL_ADC_GetValue(&hadc1)` only after successful conversion.
4. Stop the ADC with `HAL_ADC_Stop(&hadc1)`.
5. Return an explicit success/failure result so timeout or HAL errors are logged separately from a valid zero reading.

Do not recalibrate on every sample. Do not use `HAL_ADC_Start_IT()` or DMA for this one-channel, one-sample-per-100-ms requirement unless later timing or CPU requirements justify it.

## Phase 4: Implement the 100 ms cadence

Use the CMSIS-RTOS/FreeRTOS task scheduler as the cadence source. Use `osDelayUntil()` with an absolute next-wake tick rather than `osDelay(100)`, so ADC conversion and logging time do not accumulate into the sampling period.

Pseudocode:

```cpp
uint32_t nextWake = osKernelGetTickCount();

for (;;)
{
    nextWake += 100U;

    SampleResult sample = readCurrent();
    if (sample.valid)
    {
        DebugService::instance().logf(
            DebugService::Level::Debug,
            "CurrentSense raw=%lu current_mA=%lu",
            static_cast<unsigned long>(sample.raw),
            static_cast<unsigned long>(sample.current_mA));
    }
    else
    {
        DebugService::instance().log(
            DebugService::Level::Error,
            "CurrentSense ADC conversion failed");
    }

    osDelayUntil(nextWake);
}
```

Use an integer fixed-point representation such as milliamps for log output rather than relying on floating-point formatting in `printf`-style output. Keep the conversion constants explicit and document whether the calculation uses nominal or measured VDDA.

## Phase 5: Start the task

Update the application startup owned by the project, likely `User/Src/AstroWeather.cpp`:

1. Include the sensor task header.
2. Start `CurrentSenseTask::instance()` after ADC calibration has completed.
3. Ensure `DebugService::init()` and `DebugService` task startup occur before the first current log is emitted.
4. Avoid starting the sensor task from generated code outside a `USER CODE` block.

The exact startup location must be checked against the current `AstroWeather_Init()` and `DebugService` startup ordering before implementation. If calibration is placed in the sensor task, the task must not run before the ADC peripheral has been initialized.

## Phase 6: Accuracy and calibration

Initial implementation should log raw ADC and calculated current. Then add or verify:

- Actual VDDA measurement, preferably using the STM32 internal `VREFINT` channel if VDDA variation matters.
- A measured zero-current ADC offset, subtracted before current conversion.
- A known-load comparison against a trusted ammeter or multimeter.
- Saturation checks for ADC full scale and INA180 output range.
- Confirmation that the amplifier output never exceeds VDDA or the ADC input absolute limits.

The nominal scale at VDDA = 3.3 V is approximately:

```text
0.322 mA per ADC count
```

The nominal full-scale current is approximately:

```text
3.3 V / 2.5 V/A = 1.32 A
```

Actual usable range must include output headroom and offset.

## Validation plan

### Build validation

- Regenerate CubeMX code after any `.ioc` change.
- Build `Debug-HostController`.
- Confirm no duplicate ADC handle declarations or task symbols.
- Confirm the new `User/Src/Sensors/*.cpp` file is included automatically by the existing recursive CMake glob.

### Runtime validation

- Confirm one `CurrentSense` log appears approximately every 100 ms.
- Confirm log timestamps advance by approximately 100 ms, allowing for scheduler and USB transmission jitter.
- Confirm zero-load readings are stable and record the offset.
- Apply a known load and verify current direction, scale, and settling behavior.
- Confirm ADC timeout/error logging works with the input disconnected or conversion deliberately blocked during a diagnostic test.
- Monitor task stack high-water marks and heap usage through the existing debug statistics.

### Expected limitation

Because the input filter time constant is 100 ms, the measured value will settle slowly after a load change. The 100 ms logging interval is appropriate for monitoring, but it should not be interpreted as a fast protection or transient-detection path.
