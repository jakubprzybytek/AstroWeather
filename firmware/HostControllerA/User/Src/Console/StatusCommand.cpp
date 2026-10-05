#include <Console/StatusCommand.hpp>

#include <Debug/FirmwareInfo.hpp>
#include <Debug/LogService.hpp>
#include <Device/Eeprom24AA04.hpp>
#include <Settings/SettingsStore.hpp>
#include <Astro/ApiTarget.hpp>
#include <Astro/AstroDataRefreshTask.hpp>
#include <Clock/CalendarDate.hpp>
#include <Clock/ClockTask.hpp>
#include <Display/LowBrightness.hpp>
#include <WiFi/St67HttpFetchTask.hpp>

#include "FreeRTOS.h"
#include "app_config.h"
#include "cmsis_os2.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace Console {
namespace {

// The whole reply is one burst, so it must stay under the 16-line log queue;
// see HelpCommand.cpp. It is currently 14 lines after "OK status".

void line(const char* format, ...)
{
    // Room for the api line: a 64-character host and path plus the prefix.
    char text[168];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    LogService::instance().sendLine(text);
}

// Elapsed time as d hh:mm:ss. The 1 kHz tick wraps after ~49 days.
void formatDuration(uint32_t milliseconds, char* out, std::size_t size)
{
    const uint32_t seconds = milliseconds / 1000U;
    std::snprintf(out, size, "%lud %02lu:%02lu:%02lu",
                  static_cast<unsigned long>(seconds / 86400U),
                  static_cast<unsigned long>((seconds / 3600U) % 24U),
                  static_cast<unsigned long>((seconds / 60U) % 60U),
                  static_cast<unsigned long>(seconds % 60U));
}

void reportWifi(const Settings::Values& values)
{
    if (values.wifiSsid[0] == '\0') {
        line("wifi       not configured; set credentials with 'wifi set <ssid> <password>'");
        return;
    }
    using namespace HostController;
    const WifiConnectSummary last = LastWifiConnect();
    if (last.result == WifiConnectResult::NeverTried) {
        line("wifi       '%s' stored; not connected since boot ('wifi test' to try)",
             values.wifiSsid);
        return;
    }
    char ago[24];
    formatDuration(osKernelGetTickCount() - last.tick, ago, sizeof(ago));
    if (last.result == WifiConnectResult::Connected) {
        line("wifi       '%s' stored; last connect ok %s ago (channel %lu, %ld dBm)",
             values.wifiSsid, ago, static_cast<unsigned long>(last.channel),
             static_cast<long>(last.rssi));
    } else {
        line("wifi       '%s' stored; last connect FAILED %s ago: %s", values.wifiSsid, ago,
             wifiConnectResultName(last.result));
    }
}

void reportEeprom(Device::Eeprom24AA04* eeprom, Settings::Store* settings)
{
    if (eeprom == nullptr) {
        line("eeprom     not present in this variant");
        return;
    }
    if (eeprom->probe() == HAL_OK) {
        line("eeprom     answering at 0x%02X, %u bytes", static_cast<unsigned>(eeprom->address()),
             static_cast<unsigned>(Device::Eeprom24AA04::kSize));
    } else {
        line("eeprom     NOT ANSWERING at 0x%02X", static_cast<unsigned>(eeprom->address()));
    }

    if (settings == nullptr) {
        return;
    }
    const Settings::Values& values = settings->values();
    line("settings   loaded at boot: %s; adc log %s, adc display %s, time display %s, "
         "trim %+ld ppm, low brightness %s",
         Settings::Store::describe(settings->lastDecode()), values.adcLogEnabled ? "on" : "off",
         values.adcDisplayEnabled ? "on" : "off", values.clockDisplayEnabled ? "on" : "off",
         static_cast<long>(values.clockTrimPpm), values.lowBrightness ? "on" : "off");
    reportWifi(values);
}

void reportSchedule()
{
    using namespace HostController;
    const ScheduleSummary schedule = AstroDataRefreshTask::instance().schedule();
    char last[24] = "none since power-up";
    if (schedule.hasSuccess) {
        const Calendar::DateTime at = Calendar::fromSecondsSince2000(schedule.lastSuccess);
        std::snprintf(last, sizeof(last), "%04u-%02u-%02u %02u:%02u",
                      static_cast<unsigned>(at.year), static_cast<unsigned>(at.month),
                      static_cast<unsigned>(at.day), static_cast<unsigned>(at.hour),
                      static_cast<unsigned>(at.minute));
    }
    char next[48] = "once the clock is set";
    if (schedule.failures != 0U && schedule.waitingForNextSlot) {
        std::snprintf(next, sizeof(next), "no WiFi credentials");
    } else if (schedule.failures != 0U && schedule.retryInMs != 0U) {
        std::snprintf(next, sizeof(next), "retry %lu in %lu s",
                      static_cast<unsigned long>(schedule.failures),
                      static_cast<unsigned long>(schedule.retryInMs / 1000U));
    } else if (schedule.failures != 0U) {
        std::snprintf(next, sizeof(next), "retry %lu now", static_cast<unsigned long>(schedule.failures));
    }
    if (schedule.timeSet && (schedule.failures == 0U || schedule.waitingForNextSlot)) {
        const Calendar::DateTime at = Calendar::fromSecondsSince2000(schedule.nextSlot);
        const std::size_t used = (schedule.failures != 0U) ? std::strlen(next) : 0U;
        std::snprintf(next + used, sizeof(next) - used, "%s%02u:%02u", used != 0U ? ", then " : "",
                      static_cast<unsigned>(at.hour), static_cast<unsigned>(at.minute));
    }
    line("schedule   every %s from 00:10; next %s; last ok %s",
         schedule.intervalMinutes == 60U ? "hour (storm night)" : "6 h", next, last);
}

// Minutes from a server fetch time to now, when the clock is set and the fetch
// is not in the future. Both are local wall-clock times.
bool minutesSince(const Calendar::DateTime& t, uint32_t& minutes)
{
    ClockTask::DateTime now{};
    if (!ClockTask::instance().isTimeSet() || !ClockTask::instance().readDateTime(now)) {
        return false;
    }
    const uint32_t nowSeconds = Calendar::secondsSince2000(now.year, now.month, now.day,
                                                           now.hour, now.minute, now.second);
    const uint32_t fetchSeconds =
        Calendar::secondsSince2000(t.year, t.month, t.day, t.hour, t.minute, t.second);
    if (nowSeconds < fetchSeconds) {
        return false;
    }
    minutes = (nowSeconds - fetchSeconds) / 60U;
    return true;
}

// The server's last weather fetch, as the last parsed response reported it.
void reportWeatherFetch(const HostController::RefreshSummary& last)
{
    const HostController::AstroFetchTime& fetch = last.lastWeatherFetch;
    if (!last.fetchTimesKnown) {
        line("weather    last fetch time unknown until a refresh succeeds");
        return;
    }
    if (!fetch.present) {
        line("weather    last fetch time not reported by the server");
        return;
    }
    if (!fetch.valid) {
        line("weather    last fetch time malformed in the response");
        return;
    }
    if (!fetch.available) {
        line("weather    none on the server at the last refresh");
        return;
    }
    const Calendar::DateTime& t = fetch.value;
    char age[40] = "";
    uint32_t minutes = 0U;
    if (minutesSince(t, minutes)) {
        std::snprintf(age, sizeof(age), ", %lu h %02lu min ago",
                      static_cast<unsigned long>(minutes / 60U),
                      static_cast<unsigned long>(minutes % 60U));
    }
    char offset[8];
    HostController::formatUtcOffset(fetch.utcOffset, offset);
    line("weather    last fetched by the server %04u-%02u-%02u %02u:%02u:%02u%s%s%s",
         static_cast<unsigned>(t.year), static_cast<unsigned>(t.month),
         static_cast<unsigned>(t.day), static_cast<unsigned>(t.hour),
         static_cast<unsigned>(t.minute), static_cast<unsigned>(t.second),
         offset[0] != '\0' ? " " : "", offset, age);
}

// The server's last fetch of each aurora feed, on one line: the reply must
// stay under the log queue depth. An age when the clock is set, else the time.
void reportAuroraFetch(const HostController::RefreshSummary& last)
{
    using namespace HostController;
    if (!last.fetchTimesKnown) {
        line("aurora     last fetch times unknown until a refresh succeeds");
        return;
    }
    char feeds[136] = "";
    std::size_t used = 0U;
    for (std::size_t feed = 0U; feed < kAuroraFeedCount; ++feed) {
        const AstroFetchTime& fetch = last.lastAuroraFetch[feed];
        char when[24] = "none";
        uint32_t minutes = 0U;
        if (!fetch.present) {
            std::snprintf(when, sizeof(when), "not sent");
        } else if (!fetch.valid) {
            std::snprintf(when, sizeof(when), "malformed");
        } else if (fetch.available && minutesSince(fetch.value, minutes)) {
            std::snprintf(when, sizeof(when), "%lu h %02lu min ago",
                          static_cast<unsigned long>(minutes / 60U),
                          static_cast<unsigned long>(minutes % 60U));
        } else if (fetch.available) {
            std::snprintf(when, sizeof(when), "%02u-%02u %02u:%02u",
                          static_cast<unsigned>(fetch.value.month),
                          static_cast<unsigned>(fetch.value.day),
                          static_cast<unsigned>(fetch.value.hour),
                          static_cast<unsigned>(fetch.value.minute));
        }
        const int written = std::snprintf(feeds + used, sizeof(feeds) - used, "%s%s %s",
                                          feed != 0U ? ", " : "", kAuroraFeedNames[feed], when);
        if (written > 0) {
            used = std::min(sizeof(feeds) - 1U, used + static_cast<std::size_t>(written));
        }
    }
    line("aurora     last fetched by the server: %s", feeds);
}

void reportAstro()
{
    using namespace HostController;
    const RefreshSummary last = AstroDataRefreshTask::instance().lastRefresh();

    // WiFi exposes no link state of its own, so the last refresh is the real
    // evidence of whether the network path works.
    if (last.outcome == RefreshOutcome::Never) {
        line("astro      %s", last.running ? "first refresh running now"
                                           : "no refresh since boot; try 'astro refresh'");
        return;
    }

    char ago[24];
    formatDuration(osKernelGetTickCount() - last.finishedTick, ago, sizeof(ago));
    if (last.outcome == RefreshOutcome::FetchFailed) {
        // An HTTP code only means something for an HTTP failure. Even then, 505
        // is the fetcher's placeholder until a response arrives, not a reply
        // from this server, so it means none came back.
        char detail[40];
        if (last.fetchStatus != St67FetchStatus::HttpFailure) {
            std::snprintf(detail, sizeof(detail), "%s", fetchStatusName(last.fetchStatus));
        } else if (last.httpStatus == 505U) {
            std::snprintf(detail, sizeof(detail), "no HTTP response");
        } else {
            std::snprintf(detail, sizeof(detail), "http %u",
                          static_cast<unsigned>(last.httpStatus));
        }
        line("astro      last refresh %s (%s), %s ago, from %s%s",
             refreshOutcomeName(last.outcome), detail, ago, refreshTriggerName(last.trigger),
             last.running ? "; another running now" : "");
    } else {
        line("astro      last refresh %s, %s ago, from %s%s", refreshOutcomeName(last.outcome), ago,
             refreshTriggerName(last.trigger), last.running ? "; another running now" : "");
    }
}

void reportRemoteBoards(Display::Display* display)
{
    if (display == nullptr) {
        line("remote     no display boards in this variant");
        return;
    }
    // Probed now rather than taken from the last refresh, so a board plugged in
    // or removed since shows up correctly.
    // The host's own address reads "host"; one outside the chain is added at
    // the end, as it has no block.
    char text[144];
    int used = std::snprintf(text, sizeof(text), "remote    ");
    for (uint8_t position = 0U; position < Display::kChainLength; ++position) {
        if (used < 0 || static_cast<std::size_t>(used) >= sizeof(text)) {
            break;
        }
        const char* state = "host";
        if (!display->isLocal(position)) {
            Display::DisplayBoard* board = display->remoteBoard(position);
            if (board == nullptr) {
                continue;
            }
            state = board->present() ? "yes" : "no";
        }
        used += std::snprintf(&text[used], sizeof(text) - static_cast<std::size_t>(used),
                              " 0x%02X %s", static_cast<unsigned>(Display::chainAddress(position)),
                              state);
    }
    if (!display->localInChain() && used >= 0 && static_cast<std::size_t>(used) < sizeof(text)) {
        std::snprintf(&text[used], sizeof(text) - static_cast<std::size_t>(used),
                      "; host 0x%02X has no block", static_cast<unsigned>(display->localAddress()));
    }
    LogService::instance().sendLine(text);
}

} // namespace

// The local board's own refresh: a late shift is a refresh interrupt that
// came later than a whole pass, which shows as one pass kept on; the
// interrupt time is what every other interrupt has to wait for.
// The brightness in use, and the local board's refresh when it reports one.
void reportLocalRefresh(Display::Display* display)
{
    const char* brightness = LowBrightness::isEnabled() ? "low" : "normal";
    Display::DisplayBoard::RefreshStats stats{};
    if (display == nullptr || !display->local().refreshStats(stats)) {
        line("display    %s brightness", brightness);
        return;
    }
    line("display    %s brightness; %lu frames, %lu late shifts, %lu late interrupts, "
         "refresh interrupt up to %lu us",
         brightness, static_cast<unsigned long>(stats.frames),
         static_cast<unsigned long>(stats.lateShifts),
         static_cast<unsigned long>(stats.lateInterrupts),
         static_cast<unsigned long>(stats.maxInterruptMicros));
}

CommandResult handleStatusCommand(const char* command, Display::Display* display,
                                  Device::Eeprom24AA04* eeprom, Settings::Store* settings)
{
    if (std::strcmp(command, "status") != 0) {
        return CommandResult::NotHandled;
    }

    char uptime[24];
    formatDuration(osKernelGetTickCount(), uptime, sizeof(uptime));

    LogService::instance().sendLine("OK status");
    line("firmware   %s, built %s", firmwareVariant(), firmwareBuildTime());
    line("uptime     %s", uptime);
    line("heap       %lu B free, %lu B lowest since boot",
         static_cast<unsigned long>(xPortGetFreeHeapSize()),
         static_cast<unsigned long>(xPortGetMinimumEverFreeHeapSize()));
    line("stats      %s", LogService::instance().statsEnabled() ? "on, every 5 s" : "off");
    reportEeprom(eeprom, settings);
    reportAstro();
    const HostController::RefreshSummary lastRefresh =
        HostController::AstroDataRefreshTask::instance().lastRefresh();
    reportWeatherFetch(lastRefresh);
    reportAuroraFetch(lastRefresh);
    reportSchedule();
    {
        const HostController::ApiTarget target = HostController::resolveApiTarget(settings);
        line("api        %s://%s%s (%s)", (APP_ST67_HTTP_USE_TLS != 0) ? "https" : "http",
             target.host, target.path,
             (target.hostSaved || target.pathSaved) ? "saved" : "built-in");
    }
    reportLocalRefresh(display);
    reportRemoteBoards(display);
    return CommandResult::Ok;
}

} // namespace Console
