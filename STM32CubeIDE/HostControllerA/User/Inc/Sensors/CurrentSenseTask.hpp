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

    Display::Display* display_ = nullptr;
};
