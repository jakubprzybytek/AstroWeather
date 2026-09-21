#include <Console/HelpCommand.hpp>

#include <Debug/LogService.hpp>

#include <cstddef>
#include <cstdio>
#include <cstring>

namespace Console {
namespace {

// Every reply burst - the index or any one group - must stay under the log
// queue depth (LogService::kLogQueueDepth, 16). The console and log tasks share
// a priority, so a whole burst is queued before any of it is transmitted, and
// the queue drops its oldest line when full. That limit is why help is split
// into groups rather than printed in full.
constexpr std::size_t kMaxBurstLines = 15U;

struct Group
{
    const char* name;
    const char* const* lines;
    std::size_t count;
};

const char* const kIndex[] = {
    "Commands. Type 'help <group>' for details and examples.",
    "  status            firmware, uptime, memory, EEPROM, WiFi, astro, boards",
    "  stats on|off      memory, stack and log statistics every 5 s (off at boot)",
    "  display ...       numbers, times and matrix rows on this board",
#if defined(FIRMWARE_VARIANT_HostController)
    "  astro refresh     fetch the sky forecast and publish it to all boards",
#endif
    "  adc ...           current-sense logging and readout (saved)",
    "  settings ...      show, save or reset the saved settings",
    "  wifi ...          store or clear WiFi credentials (saved)",
    "  eeprom ...        raw EEPROM access, for bring-up and debugging",
#if defined(FIRMWARE_VARIANT_HostController)
    "Groups: stats, display, astro, adc, settings, wifi, eeprom",
#else
    "Groups: stats, display, adc, settings, wifi, eeprom",
#endif
};

const char* const kStats[] = {
    "stats on|off",
    "    Every 5 s, print log counters, heap usage and the stack headroom of",
    "    each task, whether or not anything else is being logged. Off at every",
    "    boot and not saved.",
    "    [STATS] sent      log lines transmitted",
    "            dropped   lines lost because the log queue was full",
    "            busyDrop  lines lost because USB stayed busy or no host was open",
    "    [MEM]   heapFree  FreeRTOS heap free now; heapMin the lowest it has been",
    "    [STACK] remaining bytes of that task's stack never yet used",
    "    e.g. 'stats on'",
};

const char* const kDisplay[] = {
    "display set <n> <value> <precision>",
    "    Fixed-point number on numeric display n (0-3). value is an integer",
    "    from -999 to 9999; precision (0-3) is how many digits follow the point.",
    "    e.g. 'display set 0 1234 2' shows 12.34, 'display set 1 -45 0' shows -45",
    "display time <n> <HH:MM>",
    "    Time on display n. HH and MM accept 00-99 and are not checked as a clock.",
    "    e.g. 'display time 2 21:45'",
    "display blank <n>",
    "    Switch numeric display n off. e.g. 'display blank 3'",
    "display matrix <row> <bits>",
    "    One row of the 5x21 matrix, row 0 at the top. bits is a string of 0/1,",
    "    character N lighting column N; missing columns are off, extras ignored.",
    "    e.g. 'display matrix 0 111000111000111000111'",
    "Only this board is affected. Remote boards are updated by 'astro refresh'.",
};

#if defined(FIRMWARE_VARIANT_HostController)
const char* const kAstro[] = {
    "astro refresh",
    "    Fetch the astronomy forecast over WiFi and publish it to every board.",
    "    Runs in the background: replies 'OK astro-refresh=started' at once,",
    "    then progress and results appear in the log. Takes seconds when the",
    "    WiFi session is up, up to a couple of minutes on the first run after boot.",
    "    'ERR astro-refresh-busy'         a refresh is already running",
    "    'ERR astro-refresh-unavailable'  the WiFi task is not ready",
};
#endif

const char* const kAdc[] = {
    "adc log on|off",
    "    Log a current-sense reading 10 times a second: current (mA), MCU",
    "    temperature and supply voltage. Very noisy; switch off when done.",
    "adc display on|off",
    "    Show the measured current in mA on numeric display 0.",
    "Both are saved to the EEPROM immediately and restored at the next boot.",
    "e.g. 'adc log on', 'adc display off'",
};

const char* const kSettings[] = {
    "settings show",
    "    Settings as the firmware is using them now. The WiFi password shows",
    "    only as <set> or <unset>. 'boot-load=' is what was found in the EEPROM",
    "    at power-up: ok, blank (never saved), or an error such as bad-crc.",
    "settings save",
    "    Write the current settings to the EEPROM. Rarely needed, because adc and",
    "    wifi commands save as they change. Use it to rewrite the stored copy",
    "    after 'boot-load=' reported an error, or after 'eeprom erase'.",
    "settings defaults",
    "    Reset everything and save: adc log off, adc display on, no WiFi.",
};

const char* const kWifi[] = {
    "wifi set <ssid> <password>",
    "    Save WiFi credentials: SSID up to 32 characters, password up to 63,",
    "    neither containing spaces. This line is echoed to the log, password",
    "    included. e.g. 'wifi set AstroNet correcthorse'",
    "wifi clear",
    "    Forget the stored credentials.",
    "Note: not used for connecting yet; the firmware still connects with the",
    "credentials it was built with. Check what is stored with 'settings show'.",
};

const char* const kEeprom[] = {
    "Raw access to the 512-byte settings EEPROM. Offsets and lengths are hex,",
    "matching the addresses 'eeprom dump' prints. 000-07F holds the settings.",
    "eeprom probe      check the chip answers; reports address, size, page size",
    "eeprom scan       list every device answering on the I2C bus",
    "eeprom dump       print all 512 bytes, 16 per line",
    "eeprom read <offset> [length]",
    "    Print length bytes (default 1). e.g. 'eeprom read 070 10' prints 070-07F",
    "eeprom write <offset> <hex-bytes>",
    "    Write up to 32 bytes, given as hex digits without spaces.",
    "    e.g. 'eeprom write 100 A55A01' writes A5 5A 01 at 100-102",
    "eeprom erase",
    "    Fill all 512 bytes with FF. This ERASES THE SETTINGS; the next boot",
    "    uses defaults unless you run 'settings save' first.",
};

template <std::size_t N>
constexpr Group group(const char* name, const char* const (&lines)[N])
{
    static_assert(N + 1U <= kMaxBurstLines, "help burst would overflow the log queue");
    return Group{name, lines, N};
}

const Group kGroups[] = {
    group("stats", kStats),
    group("display", kDisplay),
#if defined(FIRMWARE_VARIANT_HostController)
    group("astro", kAstro),
#endif
    group("adc", kAdc),
    group("settings", kSettings),
    group("wifi", kWifi),
    group("eeprom", kEeprom),
};

static_assert((sizeof(kIndex) / sizeof(kIndex[0])) + 1U <= kMaxBurstLines,
              "help index would overflow the log queue");

void send(const char* const* lines, std::size_t count)
{
    for (std::size_t index = 0U; index < count; ++index) {
        LogService::instance().sendLine(lines[index]);
    }
}

} // namespace

CommandResult handleHelpCommand(const char* line)
{
    if (std::strcmp(line, "help") == 0) {
        LogService::instance().sendLine("OK help");
        send(kIndex, sizeof(kIndex) / sizeof(kIndex[0]));
        return CommandResult::Ok;
    }
    if (std::strncmp(line, "help ", 5U) != 0) {
        return CommandResult::NotHandled;
    }

    const char* topic = &line[5];
    for (const Group& entry : kGroups) {
        if (std::strcmp(topic, entry.name) == 0) {
            char header[32];
            std::snprintf(header, sizeof(header), "OK help %s", entry.name);
            LogService::instance().sendLine(header);
            send(entry.lines, entry.count);
            return CommandResult::Ok;
        }
    }

    char message[128];
    std::snprintf(message, sizeof(message), "ERR unknown help group '%.24s'; %s", topic,
                  kIndex[(sizeof(kIndex) / sizeof(kIndex[0])) - 1U]);
    LogService::instance().sendLine(message);
    return CommandResult::Ok;
}

} // namespace Console
