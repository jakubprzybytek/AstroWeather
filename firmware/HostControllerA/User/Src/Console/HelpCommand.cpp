#include <Console/HelpCommand.hpp>

#include <Console/PacedOutput.hpp>
#include <Debug/ErrorLog.hpp>
#include <Debug/LogService.hpp>

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace Console {
namespace {

// One command: its usage, a one-line summary for the index, and details for
// 'help <group>', one line per '\n', printed indented under the usage. The
// details do not repeat the summary, which is printed above them.
struct Command
{
    const char* usage;
    const char* summary;
    const char* details;  // nullptr for none
};

struct Group
{
    const char* name;
    const char* title;
    const Command* commands;
    std::size_t count;
    const char* notes;  // after the commands, not indented; nullptr for none
};

template <std::size_t N>
constexpr Group group(const char* name, const char* title, const Command (&commands)[N],
                      const char* notes = nullptr)
{
    return Group{name, title, commands, N, notes};
}

// Text lines stay within 80 characters, so with the log prefix a line fits a
// 100-column terminal; summaries within 47, after the index's usage column of 34.

const Command kStatus[] = {
    {"status", "firmware, memory, WiFi, astro, schedule, boards",
     "firmware   variant and build time\n"
     "uptime     d hh:mm:ss since boot (wraps after about 49 days)\n"
     "heap       FreeRTOS heap free now and the lowest since boot\n"
     "stats      whether the 5 s statistics are on\n"
     "eeprom     whether the settings EEPROM answers at 0x50\n"
     "settings   the boot-time load (ok, blank, bad-crc, ...), then the values\n"
     "wifi       the stored SSID and the last connection: ok with channel and\n"
     "           RSSI, or FAILED and why\n"
     "astro      the last refresh: ok, fetch-failed (http 404), crc-failed, ...;\n"
     "           how long ago, and what started it\n"
     "weather    when the server last fetched the weather\n"
     "aurora     when the server last fetched each aurora feed\n"
     "schedule   the next scheduled refresh or retry, and the last success\n"
     "api        the URL the next fetch uses, saved or built-in\n"
     "display    brightness in use, and this board's refresh counters\n"
     "remote     each address 0x10-0x15: host, or whether a board answers now"},
};

const Command kStats[] = {
    {"stats on|off", "memory, stack and log counters every 5 s",
     "Once now, then every 5 s whatever else is logged. Not saved.\n"
     "[STATS] sent      log lines transmitted\n"
     "        dropped   lines lost because the log queue was full\n"
     "        busyDrop  lines lost because USB stayed busy or no host was open\n"
     "[MEM]   heapFree  FreeRTOS heap free now; heapMin the lowest it has been\n"
     "[STACK] per task: the stack configured and the bytes never yet used\n"
     "e.g. 'stats on'"},
};

static_assert(ErrorLog::kCapacity == 24U && ErrorLog::kTextSize == 160U,
              "the errors help quotes the error log's size");

const Command kErrors[] = {
    {"errors", "list the last 24 warnings and errors",
     "One line per problem, the one quiet longest first, the latest last:\n"
     "  W 2026-10-06 08:43:09: DisplayBoard 0x15 unreachable status=1 ...\n"
     "  E 2026-09-27 13:30:02 4x, first 2026-09-27 12:20:31: <message>\n"
     "E is an error, W a warning. A repeat of a message bumps its count and\n"
     "moves it to the end instead of taking a new line. An uptime stamp\n"
     "('up 0d 00:00:42') means the clock was not set; one from an earlier boot\n"
     "says '(previous boot)' or '(<n> boots ago)'. Kept over a reset, a crash\n"
     "and flashing, not a power loss. Messages are kept to 159 characters;\n"
     "when all 24 entries are used, the one quiet longest is dropped."},
    {"errors clear", "empty the error log",
     "Also zeroes the dropped count; the boot count stays."},
};

const Command kDisplay[] = {
    {"display <board> <command> ...", "show to clear on another board, or on all",
     "<board> is 0x10-0x15, the board at that address as 'status' lists them,\n"
     "or 'all' for every board; without it a command changes this board. A\n"
     "remote board keeps the change until the next astro refresh redraws it.\n"
     "Replies 'OK display 0x12', 'ERR display-unreachable 0x12' when the board\n"
     "does not answer, or 'OK display all; unreachable 0x13 0x14'.\n"
     "e.g. 'display 0x12 test', 'display 0x14 show 1 12.3', 'display all clear'"},
    {"display show <n> <value>", "a number, time, ? or blank on numeric display n",
     "n is 0-3. <value> is one of:\n"
     "  12.34  -45  0.5   a number, -999.9 to 9999; the decimals set the precision\n"
     "  21:45             a time HH:MM, 00-99 each, not checked as a clock\n"
     "  ?                 the unavailable pattern: all four decimal points\n"
     "  blank             nothing\n"
     "On this board display 2 also shows the current and 3 the clock, which\n"
     "overwrite it. e.g. 'display show 0 12.34', 'display 0x12 show 1 21:45'"},
    {"display row <r> <cells>", "one matrix row, in the forecast's alphabet",
     "r is 0-4, 0 at the top. One character per column, up to 21:\n"
     "  0-3  that level, steady        a-c  levels 1-3, blinking\n"
     "  *    full level, blinking      . ?  off\n"
     "Missing columns are off. On this board row 4 also shows refresh progress.\n"
     "e.g. 'display row 0 0123abc*0123abc*012300' shows every kind"},
    {"display blink <n> off|colon|all", "blink nothing, the colon, or all of display n",
     "n is 0-3; the colon is L1 and L2. e.g. 'display blink 3 colon'"},
    {"display level <n> <0-3>", "brightness level of display n; 3 is full",
     "All segments of numeric display n (0-3). The levels are set by the pass\n"
     "table, see 'display passes'. e.g. 'display level 1 2'"},
    {"display test", "all lit, every level steady and blinking",
     "Levels 0-3 run along the matrix columns and the sixteen digits. Each\n"
     "digit's decimal point blinks at the digit's level and the colons at\n"
     "level 3; matrix rows 0, 2 and 4 are steady, row 1 blinks, row 3 blinks\n"
     "every other column. For judging the levels and blinking by eye."},
    {"display clear", "everything off, no blinking, full levels", nullptr},
    {"display passes [<a> <b> <c> <d>]", "show or set the pass lengths behind the levels",
     "This board only. Percent of a slot, summing to 100; default 12 31 27 30.\n"
     "Takes effect at once and is not saved: for tuning the levels by eye.\n"
     "Replies 'OK display passes=12/31/27/30'; with a <board>,\n"
     "'ERR unsupported-remote'. e.g. 'display passes 12 31 27 30'"},
    {"display low [on|off]", "low brightness on every board (saved)",
     "Drives LOW_POWER_ENABLE, which every board shares, so it takes no\n"
     "<board>. Saved and applied at boot; switch 2 toggles it too. Without\n"
     "on|off it shows the state in use and the saved one:\n"
     "'OK display-low=on saved=on'."},
};

const Command kAstro[] = {
    {"astro refresh", "fetch the forecast and show it on every board",
     "Runs in the background: replies 'OK astro-refresh=started' at once, then\n"
     "progress and the result appear in the log. Seconds with the WiFi session\n"
     "up, up to a couple of minutes on the first run after boot. Progress fills\n"
     "this board's bottom matrix row; the forecast replaces it, and a failure\n"
     "blinks where it stopped for a minute.\n"
     "'ERR astro-refresh-busy'         a refresh is already running\n"
     "'ERR astro-refresh-unavailable'  the refresh task is not ready"},
    {"astro test", "fetch the demo forecast once: every variant",
     "Like 'astro refresh', but fetches /device/astro/test (with the key), which\n"
     "shows every display variant. The saved path, the schedule and the weather\n"
     "status stay; the next refresh brings the real forecast back."},
};

const Command kApi[] = {
    {"api show", "the server host, path and key in use",
     "'api' alone does the same. Three lines, e.g. 'OK api host=<host> (saved)',\n"
     "'OK api path=<path> (built-in)', 'OK api key=<set> (saved)'. The key\n"
     "itself is never shown."},
    {"api host <host>", "save the server host name",
     "A bare host name, 1-64 characters: no scheme, port or path; HTTPS on\n"
     "port 443. e.g. 'api host api.example.com'"},
    {"api path <path>", "save the path the forecast is fetched from",
     "Starts with /, 1-32 characters, no spaces. The API wants\n"
     "/device/astro/<configurationId>. e.g. 'api path /device/astro/wroclaw'"},
    {"api key <key>", "save the API's device key",
     "1-32 letters, digits or - _ . ~; sent as ?key=<key>. Never shown again,\n"
     "but the typed line stays in the terminal's scroll-back."},
    {"api default", "forget the saved host, path and key",
     "Uses the built-in values from app_credentials.h again, and shows them."},
};

const Command kTime[] = {
    {"time show", "date and time to the ms, and the trim in use",
     "e.g. 'OK time=2026-09-22 20:15:03.123 set=yes trim=+18400ppm\n"
     "prediv=3/8146 calm=26'. set=no: not set since a power loss."},
    {"time set <YYYY-MM-DD> <HH:MM[:SS]>", "set the clock, 24-hour",
     "Seconds default to 00. The date is tracked but not shown. Kept over a\n"
     "reset or flashing; after a power loss display 3 shows --:-- until the\n"
     "clock is set again, by hand or by the first astro refresh.\n"
     "e.g. 'time set 2026-09-22 21:45' or 'time set 2026-09-22 21:45:30'"},
    {"time trim <ppm>", "correct for this board's LSI clock (saved)",
     "ppm the LSI runs fast (+) or slow (-) of 32 kHz, up to +-100000; 0 for\n"
     "none. See Docs/RTC.md to measure it. e.g. 'time trim 18372'"},
    {"time display on|off", "the time on numeric display 3 (saved)",
     "An astro refresh may overwrite display 3; the time returns at the next\n"
     "minute."},
    {"time hsi [<0-127>]", "trim this board's HSI16 clock (saved)",
     "Higher runs faster, about 0.33 % a step; 64 is the chip's default. Applies\n"
     "at once and at boot. See firmware/Docs/Display.md to measure it. Without\n"
     "a value it shows the trim, HSICAL and the saved trim:\n"
     "'OK time-hsi=63 cal=0x8E saved=63'. Not the RTC's clock: see 'time trim'."},
    {"time sync [now]", "the display boards' timeline sync",
     "The host broadcasts its refresh timeline to every board every 5 min, and\n"
     "in a burst (0, 10, 30, 60 s) at boot and when a board asks for one; 'now'\n"
     "starts a burst. Shows the broadcasts sent and answered, the next, and\n"
     "each board's answer to the last poll: locked or wants-sync."},
};

const Command kAdc[] = {
    {"adc log on|off", "log the current-sense readings at 10 Hz (saved)",
     "Current (mA), MCU temperature and supply voltage. Very noisy; switch it\n"
     "off when done. e.g. 'adc log on'"},
    {"adc display on|off", "the current in mA on numeric display 2 (saved)",
     nullptr},
};

const Command kSettings[] = {
    {"settings show", "the settings in use, six lines",
     "The WiFi password and the API key show only as <set> or <unset>.\n"
     "'boot-load=' is what the EEPROM held at power-up: ok, blank (never saved),\n"
     "or an error such as bad-crc."},
    {"settings save", "write the settings in use to the EEPROM",
     "Rarely needed: the other commands save as they change. Use it after\n"
     "'boot-load=' reported an error, or after 'eeprom erase'."},
    {"settings defaults", "reset every setting and save",
     "adc log off, adc display on, time display on, normal brightness, no clock\n"
     "trim, no WiFi, built-in api. Running tasks keep their behaviour until the\n"
     "next boot, except the api target, which the next fetch reads."},
};

const Command kWifi[] = {
    {"wifi set <ssid> [password]", "save WiFi credentials, then test them",
     "SSID 1-32 characters; WPA2 password 8-63, or leave it out for an open\n"
     "network. Quote values with spaces: wifi set \"My Network\" \"my pass phrase\"\n"
     "The line is not echoed, but stays in the terminal's scroll-back. The test\n"
     "is an ordinary astro refresh, so it also publishes the forecast."},
    {"wifi test", "test the stored credentials, in plain words",
     "A few seconds later, in plain words: passed, network not found, wrong\n"
     "password, and so on."},
    {"wifi clear", "forget the stored credentials",
     "WiFi stays off until new ones are set."},
    {"wifi stress", "bench: 100 connect-fetch cycles, about 25 min",
     "Cannot be stopped; refreshes are refused meanwhile. Logged as\n"
     "'ST67 cycle=' and 'ST67 batch-final'; then the module is powered down."},
};

const Command kEeprom[] = {
    {"eeprom probe", "whether the chip answers, its size and page", nullptr},
    {"eeprom scan", "list every device answering on the I2C bus",
     "The EEPROM answers on 0x50-0x57, remote display boards on 0x10-0x15."},
    {"eeprom dump", "print all 512 bytes, 16 per line", nullptr},
    {"eeprom read <offset> [length]", "print length bytes (default 1)",
     "e.g. 'eeprom read 070 10' prints 070-07F"},
    {"eeprom write <offset> <hex-bytes>", "write up to 32 bytes",
     "Hex digits without spaces. e.g. 'eeprom write 100 A55A01' writes A5 5A 01\n"
     "at 100-102. A write into 000-0FF changes the stored settings, and a bad\n"
     "CRC makes the next boot use defaults."},
    {"eeprom erase", "fill all 512 bytes with FF: ERASES THE SETTINGS",
     "The next boot uses defaults unless you run 'settings save' first."},
};

const Group kGroups[] = {
    group("status", "a one-screen summary of the device", kStatus),
    group("stats", "periodic memory, stack and log statistics", kStats),
    group("errors", "the error log", kErrors),
    group("display", "numbers, times and matrix rows on this or a remote board", kDisplay,
          "A bad number, cell, board or subcommand gets 'ERR invalid-argument'."),
    group("astro", "the sky forecast", kAstro,
          "Refreshes also run on a schedule ('status' shows the next) and on\n"
          "switch 1."),
    group("api", "the server the forecast comes from (saved)", kApi,
          "Changes apply from the next fetch and do not start one; run\n"
          "'astro refresh' to try them. A wrong path shows as\n"
          "'fetch-failed (http 404)', a missing key as http 401, a wrong one as 403."),
    group("time", "the clock", kTime),
    group("adc", "current sense (saved)", kAdc),
    group("settings", "the saved settings", kSettings),
    group("wifi", "WiFi credentials (saved)", kWifi,
          "'status' shows what is stored and how the last connection went."),
    group("eeprom", "raw access to the settings EEPROM, for bring-up and debugging", kEeprom,
          "Offsets and lengths are hex, as 'eeprom dump' prints them; 000-0FF\n"
          "holds the settings."),
};

// The line being sent. Static, not on the console task's stack; console task
// only.
char text[128];

// Sends `lines` one line per '\n', each after `indent`.
void sendLines(PacedOutput& out, const char* lines, const char* indent)
{
    while (lines != nullptr && *lines != '\0') {
        const char* end = std::strchr(lines, '\n');
        const std::size_t length = end != nullptr ? static_cast<std::size_t>(end - lines)
                                                  : std::strlen(lines);
        std::snprintf(text, sizeof(text), "%s%.*s", indent, static_cast<int>(length), lines);
        out.line(text);
        lines = end != nullptr ? end + 1 : nullptr;
    }
}

void sendCommand(PacedOutput& out, const Command& command)
{
    out.line(command.usage);
    sendLines(out, command.summary, "    ");
    sendLines(out, command.details, "    ");
}

void sendGroup(PacedOutput& out, const Group& group)
{
    std::snprintf(text, sizeof(text), "%s: %s", group.name, group.title);
    out.line(text);
    for (std::size_t i = 0U; i < group.count; ++i) {
        sendCommand(out, group.commands[i]);
    }
    sendLines(out, group.notes, "");
}

void sendIndex(PacedOutput& out)
{
    out.line("Commands by group. 'help <group>' explains a group, 'help <group> <command>'");
    out.line("one command (e.g. 'help display show'), 'help all' everything.");
    for (const Group& group : kGroups) {
        std::snprintf(text, sizeof(text), "-- %s: %s", group.name, group.title);
        out.line(text);
        for (std::size_t i = 0U; i < group.count; ++i) {
            std::snprintf(text, sizeof(text), "%-34s %s", group.commands[i].usage,
                          group.commands[i].summary);
            out.line(text);
        }
    }
}

// The usage starts with `topic` as whole words: "display show" matches
// "display show <n> <value>", not "display showing".
bool matches(const Command& command, const char* topic)
{
    const std::size_t length = std::strlen(topic);
    if (std::strncmp(command.usage, topic, length) != 0) {
        return false;
    }
    const unsigned char next = static_cast<unsigned char>(command.usage[length]);
    return std::isalnum(next) == 0 && next != '-';
}

const Group* findGroup(const char* name, std::size_t length)
{
    for (const Group& group : kGroups) {
        if (std::strlen(group.name) == length && std::strncmp(group.name, name, length) == 0) {
            return &group;
        }
    }
    return nullptr;
}

void sendGroupNames(const char* prefix)
{
    int used = std::snprintf(text, sizeof(text), "%s", prefix);
    for (const Group& group : kGroups) {
        if (used < 0 || static_cast<std::size_t>(used) >= sizeof(text)) {
            break;
        }
        used += std::snprintf(&text[used], sizeof(text) - static_cast<std::size_t>(used), "%s%s",
                              &group == &kGroups[0] ? "" : ", ", group.name);
    }
    LogService::instance().sendLine(text);
}

} // namespace

CommandResult handleHelpCommand(const char* line)
{
    PacedOutput out;
    if (std::strcmp(line, "help") == 0) {
        out.line("OK help");
        sendIndex(out);
        return CommandResult::Ok;
    }
    if (std::strncmp(line, "help ", 5U) != 0) {
        return CommandResult::NotHandled;
    }

    const char* topic = &line[5];
    if (std::strcmp(topic, "all") == 0) {
        out.line("OK help all");
        for (const Group& group : kGroups) {
            sendGroup(out, group);
        }
        return CommandResult::Ok;
    }

    // "<group>" or "<group> <command ...>"; every usage starts with its group.
    const std::size_t nameLength = std::strcspn(topic, " ");
    const Group* group = findGroup(topic, nameLength);
    if (group == nullptr) {
        char prefix[64];
        std::snprintf(prefix, sizeof(prefix), "ERR unknown help group '%.*s'; groups: ",
                      static_cast<int>(nameLength < 24U ? nameLength : 24U), topic);
        sendGroupNames(prefix);
        return CommandResult::Ok;
    }
    if (topic[nameLength] == '\0') {
        std::snprintf(text, sizeof(text), "OK help %s", group->name);
        out.line(text);
        sendGroup(out, *group);
        return CommandResult::Ok;
    }

    bool found = false;
    for (std::size_t i = 0U; i < group->count; ++i) {
        if (matches(group->commands[i], topic)) {
            if (!found) {
                std::snprintf(text, sizeof(text), "OK help %.100s", topic);
                out.line(text);
                found = true;
            }
            sendCommand(out, group->commands[i]);
        }
    }
    if (!found) {
        std::snprintf(text, sizeof(text), "ERR no help for '%.48s'; 'help %s' lists its commands",
                      topic, group->name);
        LogService::instance().sendLine(text);
    }
    return CommandResult::Ok;
}

} // namespace Console
