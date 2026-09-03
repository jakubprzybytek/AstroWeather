#pragma once

#include <Utils/Task.hpp>

#include <cstdint>

class CurrentSenseTask : public Task<2048>
{
public:
    static CurrentSenseTask& instance();

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
    };

    CurrentSenseTask();

    Sample readSample();
};
