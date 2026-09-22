#pragma once

#include <Utils/Task.hpp>

namespace Display {
class Display;
}

// Shows the RTC time (HH:MM) on the local board's numeric display 3.
class ClockTask : public Task<1024>
{
public:
    // Local numeric display that shows the time.
    static constexpr uint8_t kDisplayIndex = 3U;

    static ClockTask& instance();

    void setDisplay(Display::Display* display);

protected:
    void run() override;

private:
    ClockTask();

    Display::Display* display_ = nullptr;
};
