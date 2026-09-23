#pragma once

#include <cstdint>

// Pure conversions for CurrentSenseTask, so they run in the native tests. The
// VDDA and temperature arithmetic is that of the STM32G0 LL helper macros
// __LL_ADC_CALC_VREFANALOG_VOLTAGE() and __LL_ADC_CALC_TEMPERATURE() for 12-bit
// data; the factory calibration values are passed in, because the task reads
// them from system memory (VREFINT_CAL_ADDR, TEMPSENSOR_CAL1/2_ADDR), which the
// native build cannot. See docs/CurrentSense.md#conversion.
namespace CurrentSense {

// Conditions of the factory calibration, as in stm32g0xx_ll_adc.h:
// VREFINT_CAL_VREF, TEMPSENSOR_CAL_VREFANALOG, TEMPSENSOR_CAL1/2_TEMP.
constexpr uint32_t kCalibrationMilliVolts = 3000U;
constexpr int32_t kTsCal1Celsius = 30;
constexpr int32_t kTsCal2Celsius = 130;
// LL_ADC_TEMPERATURE_CALC_ERROR: the two TS_CAL values are equal.
constexpr int32_t kTemperatureCalcError = 0x7FFF;
// Used when VREFINT reads zero, which would otherwise divide by zero.
constexpr uint32_t kNominalReferenceMilliVolts = 3300U;
// The largest current the four-digit numeric display shows as a number.
constexpr uint32_t kMaxDisplayMilliAmps = 9999U;

constexpr uint32_t rawToMilliAmps(uint32_t raw,
                                  uint32_t referenceMilliVolts = 3300U)
{
    constexpr uint32_t kAdcMaxValue = 4095U;
    constexpr uint32_t kSenseScaleMilliVoltsPerAmp = 2500U;

    return static_cast<uint32_t>(
        (static_cast<uint64_t>(raw) * referenceMilliVolts * 1000ULL) /
        (static_cast<uint64_t>(kAdcMaxValue) *
         kSenseScaleMilliVoltsPerAmp));
}

// VDDA in mV from the VREFINT reading: VREFINT_CAL x 3000 / VREFINT_DATA,
// truncated. A zero reading gives kNominalReferenceMilliVolts.
constexpr uint32_t vddaMilliVolts(uint32_t vrefIntRaw, uint16_t vrefIntCal)
{
    return vrefIntRaw == 0U
        ? kNominalReferenceMilliVolts
        : (static_cast<uint32_t>(vrefIntCal) * kCalibrationMilliVolts) / vrefIntRaw;
}

// Temperature in whole degrees C: the reading rescaled to the 3.0 V of the
// calibration, then interpolated linearly between TS_CAL1 (30 C) and TS_CAL2
// (130 C), truncated toward zero. kTemperatureCalcError if TS_CAL1 == TS_CAL2.
constexpr int32_t temperatureCelsius(uint32_t temperatureRaw, uint32_t vddaMilliVolts,
                                     uint16_t tsCal1, uint16_t tsCal2)
{
    const int32_t calSpan = static_cast<int32_t>(tsCal2) - static_cast<int32_t>(tsCal1);
    if (calSpan == 0)
    {
        return kTemperatureCalcError;
    }
    const int32_t atCalibration =
        static_cast<int32_t>((temperatureRaw * vddaMilliVolts) / kCalibrationMilliVolts);
    return ((atCalibration - static_cast<int32_t>(tsCal1)) * (kTsCal2Celsius - kTsCal1Celsius)) /
               calSpan +
           kTsCal1Celsius;
}

// The value written to the numeric display: the current, or, above
// kMaxDisplayMilliAmps, a value the display cannot show, so it draws its error
// pattern (an underscore on each digit).
constexpr int16_t displayMilliAmps(uint32_t milliAmps)
{
    return milliAmps > kMaxDisplayMilliAmps ? INT16_MAX : static_cast<int16_t>(milliAmps);
}

}  // namespace CurrentSense
