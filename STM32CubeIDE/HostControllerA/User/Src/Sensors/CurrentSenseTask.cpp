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

    const uint32_t referenceMilliVolts =
        adcValues_[2] == 0U
            ? kNominalReferenceMilliVolts
            : __HAL_ADC_CALC_VREFANALOG_VOLTAGE(
                  adcValues_[2], ADC_RESOLUTION_12B);
    const int32_t temperatureCelsius = __HAL_ADC_CALC_TEMPERATURE(
        referenceMilliVolts, adcValues_[1], ADC_RESOLUTION_12B);
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
                display_->local().numeric(0U).setValue(
                    static_cast<int16_t>(sample.currentMilliAmps));
                display_->submit();
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
