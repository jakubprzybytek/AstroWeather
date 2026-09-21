#include <Console/StatusCommand.hpp>

#include <Debug/FirmwareInfo.hpp>
#include <Debug/LogService.hpp>
#include <Device/Eeprom24AA04.hpp>
#include <Settings/SettingsStore.hpp>
#if defined(FIRMWARE_VARIANT_HostController)
#include <HostController/AstroDataRefreshTask.hpp>
#endif

#include "FreeRTOS.h"
#include "cmsis_os2.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace Console {
namespace {

// The whole reply is one burst, so it must stay under the 16-line log queue;
// see HelpCommand.cpp. It is currently 10 lines.

void line(const char* format, ...)
{
    char text[128];
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
    line("settings   loaded at boot: %s; adc log %s, adc display %s",
         Settings::Store::describe(settings->lastDecode()), values.adcLogEnabled ? "on" : "off",
         values.adcDisplayEnabled ? "on" : "off");
    // Stored credentials are not yet read by the WiFi connection.
    if (values.wifiSsid[0] != '\0') {
        line("wifi       '%s' stored, but connecting uses the built-in credentials",
             values.wifiSsid);
    } else {
        line("wifi       nothing stored; connecting uses the built-in credentials");
    }
}

#if defined(FIRMWARE_VARIANT_HostController)
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
        line("astro      last refresh %s (%s, http %u), %s ago, from %s%s",
             refreshOutcomeName(last.outcome), fetchStatusName(last.fetchStatus),
             static_cast<unsigned>(last.httpStatus), ago, refreshTriggerName(last.trigger),
             last.running ? "; another running now" : "");
    } else {
        line("astro      last refresh %s, %s ago, from %s%s", refreshOutcomeName(last.outcome), ago,
             refreshTriggerName(last.trigger), last.running ? "; another running now" : "");
    }
}
#endif

void reportRemoteBoards(Display::Display* display)
{
    if (display == nullptr) {
        line("remote     no display boards in this variant");
        return;
    }
    // Probed now rather than taken from the last refresh, so a board plugged in
    // or removed since shows up correctly.
    char text[112];
    int used = std::snprintf(text, sizeof(text), "remote    ");
    for (uint8_t slot = 0U; slot < Display::Display::kRemoteSlots; ++slot) {
        Display::DisplayBoard* board = display->remoteSlot(slot);
        if (board == nullptr || used < 0 || static_cast<std::size_t>(used) >= sizeof(text)) {
            continue;
        }
        const bool present = board->present();
        used += std::snprintf(&text[used], sizeof(text) - static_cast<std::size_t>(used),
                              " 0x%02X %s", static_cast<unsigned>(board->address()),
                              present ? "yes" : "no");
    }
    LogService::instance().sendLine(text);
}

} // namespace

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
#if defined(FIRMWARE_VARIANT_HostController)
    reportAstro();
#endif
    reportRemoteBoards(display);
    return CommandResult::Ok;
}

} // namespace Console
