#include <Sensors/CurrentSenseTask.hpp>
#include <Sensors/CurrentSenseConversion.hpp>

#include <Debug/LogService.hpp>
#include <Display/Display.hpp>

#include "main.h"

namespace {

constexpr uint32_t kSamplePeriodMs = 100U;
// Local numeric display that shows the current, in mA, while 'adc display' is on.
constexpr uint8_t kCurrentDisplayIndex = 2U;
constexpr uint32_t kAdcSequenceLength = 3U;

// CurrentSenseConversion.hpp repeats the calibration conditions so it builds
// without the HAL; they must match the device header.
static_assert(CurrentSense::kCalibrationMilliVolts == VREFINT_CAL_VREF, "VREFINT_CAL_VREF");
static_assert(CurrentSense::kCalibrationMilliVolts == TEMPSENSOR_CAL_VREFANALOG,
              "TEMPSENSOR_CAL_VREFANALOG");
static_assert(CurrentSense::kTsCal1Celsius == TEMPSENSOR_CAL1_TEMP, "TEMPSENSOR_CAL1_TEMP");
static_assert(CurrentSense::kTsCal2Celsius == TEMPSENSOR_CAL2_TEMP, "TEMPSENSOR_CAL2_TEMP");
static_assert(CurrentSense::kTemperatureCalcError == LL_ADC_TEMPERATURE_CALC_ERROR,
              "LL_ADC_TEMPERATURE_CALC_ERROR");
}  // namespace

extern "C" ADC_HandleTypeDef hadc1;

CurrentSenseTask& CurrentSenseTask::instance()
{
    static CurrentSenseTask task;
    return task;
}

CurrentSenseTask::CurrentSenseTask()
    : Task<2048>("CurrentSense", osPriorityBelowNormal)
{
}

void CurrentSenseTask::setDisplay(Display::Display* display)
{
    display_ = display;
}

void CurrentSenseTask::setLoggingEnabled(bool enabled)
{
    loggingEnabled_ = enabled;
}

void CurrentSenseTask::setDisplayEnabled(bool enabled)
{
    displayEnabled_ = enabled;
}

void CurrentSenseTask::notifyAdcComplete()
{
    osThreadFlagsSet(getHandle(), kAdcCompleteFlag);
}

void CurrentSenseTask::notifyAdcError()
{
    osThreadFlagsSet(getHandle(), kAdcErrorFlag);
}

CurrentSenseTask::Sample CurrentSenseTask::readSample()
{
    osThreadFlagsClear(kAdcCompleteFlag | kAdcErrorFlag);

    if (HAL_ADC_Start_DMA(
            &hadc1, reinterpret_cast<uint32_t*>(adcValues_), kAdcSequenceLength) !=
        HAL_OK)
    {
        return {false, 0U, 0U, 0U, 0U, 0U, 0};
    }

    const uint32_t flags = osThreadFlagsWait(
        kAdcCompleteFlag | kAdcErrorFlag, osFlagsWaitAny, 10U);
    HAL_ADC_Stop_DMA(&hadc1);

    if ((flags & osFlagsError) != 0U ||
        (flags & kAdcErrorFlag) != 0U ||
        (flags & kAdcCompleteFlag) == 0U)
    {
        return {false, 0U, 0U, 0U, 0U, 0U, 0U};
    }

    // Factory calibration values, read here because the conversions are pure;
    // the sequence runs at 12 bits, the resolution they assume.
    const uint32_t referenceMilliVolts = CurrentSense::vddaMilliVolts(
        adcValues_[2], *VREFINT_CAL_ADDR);
    const int32_t temperatureCelsius = CurrentSense::temperatureCelsius(
        adcValues_[1], referenceMilliVolts, *TEMPSENSOR_CAL1_ADDR,
        *TEMPSENSOR_CAL2_ADDR);
    const uint32_t currentMilliAmps = CurrentSense::rawToMilliAmps(
        adcValues_[0], referenceMilliVolts);

    return {true, adcValues_[0], currentMilliAmps, adcValues_[1], adcValues_[2],
            referenceMilliVolts, temperatureCelsius};
}

void CurrentSenseTask::run()
{
    if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
    {
        LogService::instance().log(
            LogService::Level::Error,
            "CurrentSense ADC calibration failed");
    }

    uint32_t nextWake = osKernelGetTickCount();

    for (;;)
    {
        nextWake += kSamplePeriodMs;

        const Sample sample = readSample();
        if (sample.valid)
        {
            if (display_ != nullptr && displayEnabled_)
            {
                display_->local().numeric(kCurrentDisplayIndex).setValue(
                    CurrentSense::displayMilliAmps(sample.currentMilliAmps));
                // Only this board changes, so skip the I2C refresh of every
                // remote board that submit() would do ten times a second.
                display_->submitLocal();
            }

            if (loggingEnabled_)
            {
                LogService::instance().logf(
                    LogService::Level::Debug,
                    "CurrentSense raw=%lu current_mA=%lu temp_raw=%lu temp_C=%ld vref_raw=%lu vdda_mV=%lu",
                    static_cast<unsigned long>(sample.raw),
                    static_cast<unsigned long>(sample.currentMilliAmps),
                    static_cast<unsigned long>(sample.temperatureRaw),
                    static_cast<long>(sample.temperatureCelsius),
                    static_cast<unsigned long>(sample.vrefIntRaw),
                    static_cast<unsigned long>(sample.referenceMilliVolts));
            }
        }
        else
        {
            LogService::instance().log(
                LogService::Level::Error,
                "CurrentSense ADC conversion failed");
        }

        osDelayUntil(nextWake);
    }
}

extern "C" void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
    if (hadc == &hadc1)
    {
        CurrentSenseTask::instance().notifyAdcComplete();
    }
}

extern "C" void HAL_ADC_ErrorCallback(ADC_HandleTypeDef* hadc)
{
    if (hadc == &hadc1)
    {
        CurrentSenseTask::instance().notifyAdcError();
    }
}
