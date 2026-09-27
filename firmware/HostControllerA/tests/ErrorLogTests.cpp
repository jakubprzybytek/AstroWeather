#include <Debug/ErrorLog.hpp>

#include <Clock/CalendarDate.hpp>

#include <Expect.hpp>

#include <cstring>
#include <string>

namespace {

using ErrorLog::Level;
using ErrorLog::Stamp;
using Test::expect;
using Test::expectEqual;

std::string line(const ErrorLog::Log& log, const ErrorLog::Entry& entry)
{
    char text[200];
    log.format(entry, text, sizeof(text));
    return text;
}

Stamp up(uint32_t seconds)
{
    return {false, seconds};
}

Stamp wall(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
{
    return {true, Calendar::secondsSince2000(year, month, day, hour, minute, second)};
}

void testGarbageStartsEmpty()
{
    ErrorLog::Storage storage;
    std::memset(&storage, 0xA5, sizeof(storage));  // power-up RAM
    ErrorLog::Log log(storage);
    log.begin();
    expectEqual(log.count(), 0U, "random RAM gives an empty log");
    expectEqual(log.dropped(), 0U, "and nothing dropped");
    expectEqual(log.boot(), 1U, "first boot");
    expect(log.newest() == nullptr, "no newest entry");
}

void testRecordAndFormat()
{
    ErrorLog::Storage storage{};
    ErrorLog::Log log(storage);
    log.begin();
    log.record(Level::Error, "CurrentSense ADC conversion failed", up(42U));
    log.record(Level::Warning, "DisplayBoard 0x10 unreachable status=3", wall(2026, 9, 27, 0, 26, 54));
    expectEqual(log.count(), 2U, "two entries");
    expectEqual(line(log, log.entry(0)), std::string("E up 0d 00:00:42: CurrentSense ADC conversion failed"),
                "uptime stamp");
    expectEqual(line(log, log.entry(1)),
                std::string("W 2026-09-27 00:26:54: DisplayBoard 0x10 unreachable status=3"),
                "wall-clock stamp");
    expect(log.newest() == &log.entry(1), "newest is the last recorded");
}

void testRepeatsCollapse()
{
    ErrorLog::Storage storage{};
    ErrorLog::Log log(storage);
    log.begin();
    log.record(Level::Error, "fetch failed", up(60U));
    log.record(Level::Error, "fetch failed", up(180U));
    log.record(Level::Error, "fetch failed", wall(2026, 9, 27, 3, 10, 0));
    expectEqual(log.count(), 1U, "repeats take one entry");
    expectEqual(log.entry(0).count, 3U, "counted");
    expectEqual(line(log, log.entry(0)),
                std::string("E up 0d 00:01:00 x3, last 2026-09-27 03:10:00: fetch failed"),
                "first time kept, last time updated, clock set between");

    log.record(Level::Warning, "fetch failed", up(200U));
    expectEqual(log.count(), 2U, "same text at another level is a new entry");

    // A set repeated in turn, as each refresh logs every unreachable board.
    for (int round = 0; round < 3; ++round) {
        log.record(Level::Warning, "board 0x10 unreachable", up(300U + round));
        log.record(Level::Warning, "board 0x11 unreachable", up(300U + round));
    }
    expectEqual(log.count(), 4U, "a repeated set takes one entry each");
    expectEqual(log.entry(2).count, 3U, "first of the set counted");
    expectEqual(log.entry(3).count, 3U, "second of the set counted");

    log.record(Level::Error, "fetch failed", up(400U));
    expectEqual(log.count(), 4U, "an older entry collapses too");
    expectEqual(log.entry(0).count, 4U, "and keeps its place");
    expectEqual(ErrorLog::textHash("fetch failed"), log.entry(0).hash, "hash stored");
}

void testCapAndDropped()
{
    ErrorLog::Storage storage{};
    ErrorLog::Log log(storage);
    log.begin();
    for (uint32_t i = 0U; i < ErrorLog::kCapacity + 3U; ++i) {
        log.record(Level::Error, ("error " + std::to_string(i)).c_str(), up(i));
    }
    expectEqual(log.count(), ErrorLog::kCapacity, "capped at capacity");
    expectEqual(log.dropped(), 3U, "oldest three dropped");
    expectEqual(std::string(log.entry(0).text), std::string("error 3"), "oldest kept is the fourth");
    expectEqual(std::string(log.newest()->text), std::string("error 18"), "newest");

    log.clear();
    expectEqual(log.count(), 0U, "cleared");
    expectEqual(log.dropped(), 0U, "dropped count cleared");
    expectEqual(log.boot(), 1U, "boot count kept");
}

void testLongTextTruncated()
{
    ErrorLog::Storage storage{};
    ErrorLog::Log log(storage);
    log.begin();
    const std::string longText(300U, 'x');
    log.record(Level::Error, longText.c_str(), up(1U));
    expectEqual(std::strlen(log.entry(0).text), ErrorLog::kTextSize - 1U, "truncated");
    log.record(Level::Error, longText.c_str(), up(2U));
    expectEqual(log.count(), 1U, "a truncated repeat still collapses");
}

void testSurvivesReboot()
{
    ErrorLog::Storage storage{};
    {
        ErrorLog::Log log(storage);
        log.begin();
        log.record(Level::Error, "before reset", up(30U));
        log.record(Level::Error, "stamped", wall(2026, 9, 27, 1, 2, 3));
    }
    ErrorLog::Log afterReset(storage);
    afterReset.begin();
    expectEqual(afterReset.boot(), 2U, "boot counted");
    expectEqual(afterReset.count(), 2U, "entries kept");
    expectEqual(line(afterReset, afterReset.entry(0)),
                std::string("E up 0d 00:00:30 (previous boot): before reset"),
                "uptime from the previous boot marked");
    expectEqual(line(afterReset, afterReset.entry(1)), std::string("E 2026-09-27 01:02:03: stamped"),
                "wall-clock stamp needs no mark");

    afterReset.record(Level::Error, "stamped", wall(2026, 9, 27, 1, 5, 0));
    expectEqual(afterReset.count(), 3U, "a repeat from another boot is a new entry");

    ErrorLog::Log later(storage);
    later.begin();
    later.begin();
    expectEqual(line(later, later.entry(0)), std::string("E up 0d 00:00:30 (3 boots ago): before reset"),
                "older boots counted");
}

void testCorruptionDetected()
{
    ErrorLog::Storage storage{};
    ErrorLog::Log log(storage);
    log.begin();
    log.record(Level::Error, "kept", up(1U));

    ErrorLog::Storage copy = storage;
    copy.check ^= 1U;
    ErrorLog::Log badCheck(copy);
    badCheck.begin();
    expectEqual(badCheck.count(), 0U, "bad check word starts afresh");

    copy = storage;
    copy.count = ErrorLog::kCapacity + 1U;
    ErrorLog::Log badCount(copy);
    badCount.begin();
    expectEqual(badCount.count(), 0U, "impossible count starts afresh");

    copy = storage;
    std::memset(copy.entries[0].text, 'x', ErrorLog::kTextSize);
    ErrorLog::Log badText(copy);
    badText.begin();
    expectEqual(badText.count(), 0U, "unterminated text starts afresh");

    copy = storage;
    copy.entries[0].level = 7U;
    ErrorLog::Log badLevel(copy);
    badLevel.begin();
    expectEqual(badLevel.count(), 0U, "unknown level starts afresh");
}

void testFormatStamp()
{
    char text[24];
    ErrorLog::formatStamp(false, 2U * 86400U + 3U * 3600U + 4U * 60U + 5U, text, sizeof(text));
    expectEqual(std::string(text), std::string("up 2d 03:04:05"), "uptime with days");
    ErrorLog::formatStamp(true, 0U, text, sizeof(text));
    expectEqual(std::string(text), std::string("2000-01-01 00:00:00"), "epoch");
}

} // namespace

int main()
{
    testGarbageStartsEmpty();
    testRecordAndFormat();
    testRepeatsCollapse();
    testCapAndDropped();
    testLongTextTruncated();
    testSurvivesReboot();
    testCorruptionDetected();
    testFormatStamp();
    return Test::finish("ErrorLog");
}
