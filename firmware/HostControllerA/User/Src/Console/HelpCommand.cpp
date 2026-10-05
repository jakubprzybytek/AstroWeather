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
    "  errors [clear]    the last 16 warnings and errors, kept over resets",
    "  display ...       numbers, times and matrix rows on this board",
    "  astro refresh     fetch the sky forecast and publish it to all boards",
    "  astro test        fetch the server's demo forecast once, every variant",
    "  api ...           show or set the server host, path and key (saved)",
    "  time ...          show, set and trim the clock; show it on display 3",
    "  adc ...           current-sense logging and readout (saved)",
    "  settings ...      show, save or reset the saved settings",
    "  wifi ...          set, test or clear WiFi credentials (saved)",
    "  eeprom ...        raw EEPROM access, for bring-up and debugging",
    "Groups: stats, display, astro, api, time, adc, settings, wifi, eeprom",
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
    "display show <n> <value>     numeric n (0-3): a number with up to 3 decimals,",
    "    a time HH:MM, ? (the unavailable pattern) or blank. e.g. 'display show 0 12.34'",
    "display row <r> <cells>      matrix row r (0-4, 0 at the top), one character per",
    "    column as the payload: 0-3 level, a-c level 1-3 blinking, * as c, . or ? off",
    "display blink <n> off|colon|all    blink nothing, the colon, or all of display n",
    "display level <n> <0-3>            brightness of display n; 3 is full",
    "display test                       everything lit, each level steady and blinking",
    "display clear                      everything off, no blink, full level",
    "display passes [<a> <b> <c> <d>]   show or set the pass lengths behind the levels,",
    "    percent of a slot summing to 100; default 12 31 27 30",
    "display low [on|off]               low brightness on every board; saved, and",
    "    switch 2 toggles it. 'display low' shows the state in use and the saved one.",
    "0x10-0x15 or 'all' after 'display' picks another board or every one, e.g. 'display",
    "    0x12 test'; kept until the next astro refresh. Not for 'passes' or 'low'.",
};

const char* const kApi[] = {
    "api show          the host, path and key (shown only as <set>), each saved or built-in",
    "api host <host>",
    "    Save the server host name: no scheme, port or path; HTTPS on port 443.",
    "    e.g. 'api host api.example.com'",
    "api path <path>",
    "    Save the path, starting with /, up to 32 characters. e.g. 'api path /device/astro/wroclaw'",
    "api key <key>",
    "    Save the API's device key, up to 32 letters, digits or - _ . ~; sent as ?key=.",
    "api default",
    "    Forget the saved values and use the built-in ones from app_credentials.h.",
    "Changes apply from the next fetch; run 'astro refresh' to try them.",
};

const char* const kAstro[] = {
    "astro refresh",
    "    Fetch the astronomy forecast over WiFi and publish it to every board.",
    "    Runs in the background: replies 'OK astro-refresh=started' at once,",
    "    then progress and results appear in the log. Takes seconds when the",
    "    WiFi session is up, up to a couple of minutes on the first run after boot.",
    "    'ERR astro-refresh-busy'         a refresh is already running",
    "    'ERR astro-refresh-unavailable'  the WiFi task is not ready",
    "Progress fills this board's bottom matrix row; the forecast replaces it,",
    "and a failure blinks where it stopped for a minute.",
    "astro test",
    "    Like 'astro refresh', but fetches the demo forecast (/astro/test) once:",
    "    every display variant. The path, schedule and weather status stay;",
    "    the next refresh brings the real forecast back.",
};

const char* const kTime[] = {
    "time show         date and time to the millisecond, and the trim in use",
    "time set <YYYY-MM-DD> <HH:MM[:SS]>",
    "    Set the clock, 24-hour; seconds are optional and default to 00. The",
    "    date is tracked but not shown. Kept over a reset or flashing; after a",
    "    power loss display 3 shows --:-- until it is set again.",
    "    e.g. 'time set 2026-09-22 21:45' or 'time set 2026-09-22 21:45:30'",
    "time trim <ppm>",
    "    Correct for this board's LSI running ppm fast (+) or slow (-) of 32 kHz;",
    "    0 for none. Saved. See docs/RTC.md to measure it. e.g. 'time trim 18372'",
    "time display on|off",
    "    Show the time on numeric display 3. Saved. An astro refresh may",
    "    overwrite display 3; the time returns at the next minute.",
};

const char* const kAdc[] = {
    "adc log on|off",
    "    Log a current-sense reading 10 times a second: current (mA), MCU",
    "    temperature and supply voltage. Very noisy; switch off when done.",
    "adc display on|off",
    "    Show the measured current in mA on numeric display 2.",
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
    "    Reset everything and save: adc log off, adc display on, time display on,",
    "    normal brightness, no clock trim, no WiFi, built-in api. Applies at next boot.",
};

const char* const kWifi[] = {
    "wifi set <ssid> [password]",
    "    Save WiFi credentials, then test them straight away. SSID 1-32",
    "    characters; WPA2 password 8-63, or leave it out for an open network.",
    "    Quote values containing spaces: wifi set \"My Network\" \"my pass phrase\"",
    "    The line is echoed to the log, password included.",
    "wifi test",
    "    Connect with the stored credentials and report the result in plain",
    "    words: passed, network not found, wrong password, and so on.",
    "wifi clear",
    "    Forget the stored credentials. WiFi stays off until new ones are set.",
    "wifi stress",
    "    Bench: 100 connect-fetch-disconnect cycles (~25 min, cannot be stopped),",
    "    logged as 'ST67 cycle=' and 'ST67 batch-final'; then the module is off.",
    "'status' shows what is stored and how the last connection went.",
};

const char* const kEeprom[] = {
    "Raw access to the 512-byte settings EEPROM. Offsets and lengths are hex,",
    "matching the addresses 'eeprom dump' prints. 000-0FF holds the settings.",
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
    group("astro", kAstro),
    group("api", kApi),
    group("time", kTime),
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
