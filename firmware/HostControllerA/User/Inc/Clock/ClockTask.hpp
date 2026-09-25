#pragma once

#include <Clock/CalendarDate.hpp>
#include <Clock/ClockSync.hpp>
#include <Clock/RtcTrim.hpp>
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
    // rejects it. Restarts the drift measurement of syncToServer().
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

    // Compares the RTC with `server` + `millisecond`, the time an astro API
    // response was stamped with, whose headers arrived at `responseTick`.
    // Without `hasMilliseconds` the server truncated it to whole seconds and the
    // comparison is less precise; see ClockSync::Precision. Steps the RTC when it
    // is far enough out, and logs the offset, the decision and the drift
    // measured across syncs. Blocks for up to a second when stepping, to set it
    // on a second boundary. Call from one task.
    void syncToServer(const Calendar::DateTime& server, uint16_t millisecond,
                      bool hasMilliseconds, uint32_t responseTick);
    int32_t trimPpm() const { return trimPpm_; }
    const RtcTrim::Settings& trimSettings() const { return trimSettings_; }

protected:
    void run() override;

private:
    ClockTask();

    void wake();
    bool writeDateTime(const Calendar::DateTime& dateTime);
    // Sets the RTC to the server's time at the start of its next second.
    // `serverMs` is the server time when `responseTick` was taken. Returns the
    // time set and how many ms into that second it was written.
    bool stepToServer(int64_t serverMs, uint32_t responseTick, Calendar::DateTime& set,
                      uint32_t& lateMs);
    void logDrift(const ClockSync::Result& result, const ClockSync::Precision& precision);

    static constexpr uint32_t kFlagRedraw = 1U << 0U;

    Display::Display* display_ = nullptr;
    volatile bool displayEnabled_ = true;
    volatile bool timeSet_ = false;
    int32_t trimPpm_ = 0;
    // What MX_RTC_Init() sets after a power-up; setTrim() at boot replaces it.
    RtcTrim::Settings trimSettings_{127U, 249U, 0U};
    // Only syncToServer() uses the tracker; the console asks for a reset
    // through the flag, which syncToServer() acts on.
    ClockSync::DriftTracker drift_;
    volatile bool driftResetPending_ = false;
    // Serialises RTC access between this task and setTime() callers.
    Mutex rtcMutex_;
};
