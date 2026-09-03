#include <Sensors/CurrentSenseTask.hpp>
#include <Sensors/CurrentSenseConversion.hpp>

#include <Debug/DebugService.hpp>

#include "main.h"

namespace {

constexpr uint32_t kSamplePeriodMs = 100U;
constexpr uint32_t kAdcSequenceLength = 3U;
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

CurrentSenseTask::Sample CurrentSenseTask::readSample()
{
    if (HAL_ADC_Start(&hadc1) != HAL_OK)
    {
        return {false, 0U, 0U, 0U, 0U};
    }

    uint32_t values[kAdcSequenceLength] = {0U, 0U, 0U};
    for (uint32_t index = 0U; index < kAdcSequenceLength; ++index)
    {
        if (HAL_ADC_PollForConversion(&hadc1, 10U) != HAL_OK)
        {
            HAL_ADC_Stop(&hadc1);
            return {false, 0U, 0U, 0U, 0U};
        }

        values[index] = HAL_ADC_GetValue(&hadc1);
    }

    HAL_ADC_Stop(&hadc1);

    const uint32_t currentMilliAmps =
        CurrentSense::rawToMilliAmps(values[0]);

    return {true, values[0], currentMilliAmps, values[1], values[2]};
}

void CurrentSenseTask::run()
{
    if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
    {
        DebugService::instance().log(
            DebugService::Level::Error,
            "CurrentSense ADC calibration failed");
    }

    uint32_t nextWake = osKernelGetTickCount();

    for (;;)
    {
        nextWake += kSamplePeriodMs;

        const Sample sample = readSample();
        if (sample.valid)
        {
            DebugService::instance().logf(
                DebugService::Level::Debug,
                "CurrentSense raw=%lu current_mA=%lu temp_raw=%lu vref_raw=%lu",
                static_cast<unsigned long>(sample.raw),
                static_cast<unsigned long>(sample.currentMilliAmps),
                static_cast<unsigned long>(sample.temperatureRaw),
                static_cast<unsigned long>(sample.vrefIntRaw));
        }
        else
        {
            DebugService::instance().log(
                DebugService::Level::Error,
                "CurrentSense ADC conversion failed");
        }

        osDelayUntil(nextWake);
    }
}
