#pragma once

#include <Utils/Task.hpp>

#include <cstdint>

namespace Display {
class Display;
}

class CurrentSenseTask : public Task<2048>
{
public:
    static CurrentSenseTask& instance();

    void setDisplay(Display::Display* display);
    void setLoggingEnabled(bool enabled);
    void setDisplayEnabled(bool enabled);
    void notifyAdcComplete();
    void notifyAdcError();

protected:
    void run() override;

private:
    struct Sample
    {
        bool valid;
        uint32_t raw;
        uint32_t currentMilliAmps;
        uint32_t temperatureRaw;
        uint32_t vrefIntRaw;
        uint32_t referenceMilliVolts;
        int32_t temperatureCelsius;
    };

    CurrentSenseTask();

    Sample readSample();

    static constexpr uint32_t kAdcCompleteFlag = 1U << 0U;
    static constexpr uint32_t kAdcErrorFlag = 1U << 1U;

    alignas(uint32_t) uint16_t adcValues_[3] = {0U, 0U, 0U};
    Display::Display* display_ = nullptr;
    volatile bool loggingEnabled_ = false;
    volatile bool displayEnabled_ = true;
};
