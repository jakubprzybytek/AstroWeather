#include <Console/TimeCommand.hpp>

#include <Debug/LogService.hpp>
#include <Clock/CalendarDate.hpp>
#include <Clock/ClockTask.hpp>
#include <Settings/SettingsStore.hpp>

#include <cstdio>
#include <cstring>

namespace Console {
namespace {

void persist(Settings::Store* store)
{
    if (store == nullptr) {
        return;
    }
    const HAL_StatusTypeDef status = store->save();
    if (status != HAL_OK) {
        LogService::instance().logf(LogService::Level::Error, "Settings save failed status=%u",
                                    static_cast<unsigned>(status));
    }
}

} // namespace

CommandResult handleTimeCommand(const char* line, Settings::Store* store)
{
    if (std::strcmp(line, "time") != 0 && std::strncmp(line, "time ", 5U) != 0) {
        return CommandResult::NotHandled;
    }

    const bool displayOn = std::strcmp(line, "time display on") == 0;
    const bool displayOff = std::strcmp(line, "time display off") == 0;
    if (displayOn || displayOff) {
        ClockTask::instance().setDisplayEnabled(displayOn);
        if (store != nullptr) {
            // Saved at once, as the adc toggles are, to survive a power cycle.
            store->values().clockDisplayEnabled = displayOn;
            persist(store);
        }
        LogService::instance().sendLine(displayOn ? "OK time-display=on"
                                                  : "OK time-display=off");
        return CommandResult::Ok;
    }

    char message[96];
    char extra = '\0';
    ClockTask& clock = ClockTask::instance();

    if (std::strcmp(line, "time show") == 0) {
        ClockTask::DateTime now{};
        if (!clock.readDateTime(now)) {
            return CommandResult::Unavailable;
        }
        const RtcTrim::Settings& trim = clock.trimSettings();
        std::snprintf(message, sizeof(message),
                      "OK time=%04u-%02u-%02u %02u:%02u:%02u.%03u set=%s trim=%+ldppm "
                      "prediv=%lu/%lu calm=%lu",
                      static_cast<unsigned>(now.year), static_cast<unsigned>(now.month),
                      static_cast<unsigned>(now.day), static_cast<unsigned>(now.hour),
                      static_cast<unsigned>(now.minute), static_cast<unsigned>(now.second),
                      static_cast<unsigned>(now.millisecond), clock.isTimeSet() ? "yes" : "no",
                      static_cast<long>(clock.trimPpm()),
                      static_cast<unsigned long>(trim.asynchPrediv),
                      static_cast<unsigned long>(trim.synchPrediv),
                      static_cast<unsigned long>(trim.minusPulses));
        LogService::instance().sendLine(message);
        return CommandResult::Ok;
    }

    long ppm = 0;
    if (std::sscanf(line, "time trim %ld%c", &ppm, &extra) == 1) {
        if (ppm < -RtcTrim::kMaxTrimPpm || ppm > RtcTrim::kMaxTrimPpm) {
            return CommandResult::InvalidArgument;
        }
        if (!clock.setTrim(static_cast<int32_t>(ppm))) {
            return CommandResult::Unavailable;
        }
        if (store != nullptr) {
            store->values().clockTrimPpm = static_cast<int32_t>(ppm);
            persist(store);
        }
        std::snprintf(message, sizeof(message), "OK time-trim=%+ldppm", ppm);
        LogService::instance().sendLine(message);
        return CommandResult::Ok;
    }

    unsigned year = 0U;
    unsigned month = 0U;
    unsigned day = 0U;
    unsigned hour = 0U;
    unsigned minute = 0U;
    unsigned second = 0U;
    // Seconds are optional and default to 0. %c catches trailing text that
    // neither format takes.
    const int withSeconds = std::sscanf(line, "time set %u-%u-%u %u:%u:%u%c", &year, &month, &day,
                                        &hour, &minute, &second, &extra);
    if (withSeconds != 6) {
        second = 0U;
        if (std::sscanf(line, "time set %u-%u-%u %u:%u%c", &year, &month, &day, &hour, &minute,
                        &extra) != 5) {
            return CommandResult::InvalidArgument;
        }
    }
    if (!Calendar::isValidDate(static_cast<uint16_t>(year), static_cast<uint8_t>(month),
                               static_cast<uint8_t>(day)) ||
        hour > 23U || minute > 59U || second > 59U) {
        return CommandResult::InvalidArgument;
    }
    if (!clock.setDateTime(static_cast<uint16_t>(year), static_cast<uint8_t>(month),
                           static_cast<uint8_t>(day), static_cast<uint8_t>(hour),
                           static_cast<uint8_t>(minute), static_cast<uint8_t>(second))) {
        return CommandResult::Unavailable;
    }
    std::snprintf(message, sizeof(message), "OK time=%04u-%02u-%02u %02u:%02u:%02u", year, month,
                  day, hour, minute, second);
    LogService::instance().sendLine(message);
    return CommandResult::Ok;
}

} // namespace Console
