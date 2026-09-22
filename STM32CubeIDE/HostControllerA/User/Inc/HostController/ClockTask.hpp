#pragma once

#include <HostController/RtcTrim.hpp>
#include <Utils/Mutex.hpp>
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
    // Off blanks the display and stops redrawing it.
    void setDisplayEnabled(bool enabled);
    // Sets the RTC calendar, and marks the time as set, so that it is kept
    // over a reset. Returns false for an invalid date or time, or if the RTC
    // rejects it.
    bool setDateTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour,
                     uint8_t minute, uint8_t second = 0U);
    // False after a power-up until setTime(): the RTC then counts from 00:00
    // and the display shows "--:--".
    bool isTimeSet() const { return timeSet_; }

    struct DateTime
    {
        uint16_t year;  // full year, e.g. 2026
        uint8_t month;  // 1..12
        uint8_t day;    // 1..31
        uint8_t hour;
        uint8_t minute;
        uint8_t second;
        uint16_t millisecond;
    };
    bool readDateTime(DateTime& dateTime);

    // Corrects the RTC for an LSI running `ppm` away from 32 kHz; see RtcTrim.
    // Safe before the scheduler starts. Returns false for an out-of-range value
    // or if the RTC rejects it; the previous trim then stays in effect.
    bool setTrim(int32_t ppm);
    int32_t trimPpm() const { return trimPpm_; }
    const RtcTrim::Settings& trimSettings() const { return trimSettings_; }

protected:
    void run() override;

private:
    ClockTask();

    void wake();

    static constexpr uint32_t kFlagRedraw = 1U << 0U;

    Display::Display* display_ = nullptr;
    volatile bool displayEnabled_ = true;
    volatile bool timeSet_ = false;
    int32_t trimPpm_ = 0;
    // What MX_RTC_Init() sets after a power-up; setTrim() at boot replaces it.
    RtcTrim::Settings trimSettings_{127U, 249U, 0U};
    // Serialises RTC access between this task and setTime() callers.
    Mutex rtcMutex_;
};
