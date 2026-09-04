#include <Sensors/CurrentSenseTask.hpp>
#include <Sensors/CurrentSenseConversion.hpp>

#include <Debug/LogService.hpp>
#include <Display/Display.hpp>

#include "main.h"

namespace {

constexpr uint32_t kSamplePeriodMs = 100U;
constexpr uint32_t kAdcSequenceLength = 3U;
constexpr uint32_t kNominalReferenceMilliVolts = 3300U;
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

CurrentSenseTask::Sample CurrentSenseTask::readSample()
{
    if (HAL_ADC_Start(&hadc1) != HAL_OK)
    {
        return {false, 0U, 0U, 0U, 0U, 0U, 0};
    }

    uint32_t values[kAdcSequenceLength] = {0U, 0U, 0U};
    for (uint32_t index = 0U; index < kAdcSequenceLength; ++index)
    {
        if (HAL_ADC_PollForConversion(&hadc1, 10U) != HAL_OK)
        {
            HAL_ADC_Stop(&hadc1);
            return {false, 0U, 0U, 0U, 0U, 0U, 0};
        }

        values[index] = HAL_ADC_GetValue(&hadc1);
    }

    HAL_ADC_Stop(&hadc1);

    const uint32_t referenceMilliVolts =
        values[2] == 0U
            ? kNominalReferenceMilliVolts
            : __HAL_ADC_CALC_VREFANALOG_VOLTAGE(values[2], ADC_RESOLUTION_12B);
    const int32_t temperatureCelsius = __HAL_ADC_CALC_TEMPERATURE(
        referenceMilliVolts, values[1], ADC_RESOLUTION_12B);
    const uint32_t currentMilliAmps = CurrentSense::rawToMilliAmps(
        values[0], referenceMilliVolts);

    return {true, values[0], currentMilliAmps, values[1], values[2],
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
            if (display_ != nullptr)
            {
                display_->local().numeric(0U).setValue(
                    static_cast<int16_t>(sample.currentMilliAmps));
                display_->submit();
            }

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
        else
        {
            LogService::instance().log(
                LogService::Level::Error,
                "CurrentSense ADC conversion failed");
        }

        osDelayUntil(nextWake);
    }
}
