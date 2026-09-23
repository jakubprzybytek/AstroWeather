#include <HostController/CalendarDate.hpp>
#include <HostController/RefreshSchedule.hpp>

#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char* caseName)
{
    if (!condition) {
        std::cerr << caseName << " failed\n";
        ++failures;
    }
}

constexpr uint32_t kMinuteMs = 60000U;

constexpr uint32_t at(uint8_t day, uint8_t hour, uint8_t minute, uint8_t second = 0U)
{
    return Calendar::secondsSince2000(2026U, 9U, day, hour, minute, second);
}

RefreshSchedule::Clock clock(uint32_t now, uint32_t tick)
{
    return {true, now, tick};
}

void testSlots()
{
    using RefreshSchedule::nextSlotStart;
    using RefreshSchedule::slotStart;
    expect(slotStart(at(23, 12, 10)) == at(23, 12, 10), "a slot starts on its own second");
    expect(slotStart(at(23, 12, 9, 59)) == at(23, 6, 10), "a second earlier is the previous slot");
    expect(slotStart(at(23, 23, 59)) == at(23, 18, 10), "the evening slot");
    expect(slotStart(at(24, 0, 5)) == at(23, 18, 10), "just after midnight, still the evening slot");
    expect(slotStart(at(24, 0, 10)) == at(24, 0, 10), "the midnight slot");
    expect(nextSlotStart(at(23, 12, 10)) == at(23, 18, 10), "next after the noon slot");
    expect(nextSlotStart(at(23, 20, 0)) == at(24, 0, 10), "next crosses midnight");
    expect(slotStart(0U) == 0U && nextSlotStart(0U) == 600U, "before the first slot of 2000");
    // 2026-09-30 18:10 -> 2026-10-01 00:10
    expect(nextSlotStart(at(30, 18, 30)) == Calendar::secondsSince2000(2026U, 10U, 1U, 0U, 10U, 0U),
           "next crosses a month end");
}

void testRetryDelays()
{
    expect(RefreshSchedule::retryDelayMs(0U) == 0U, "no failure, no delay");
    expect(RefreshSchedule::retryDelayMs(1U) == 2U * kMinuteMs, "first retry after 2 min");
    expect(RefreshSchedule::retryDelayMs(4U) == 20U * kMinuteMs, "fourth retry after 20 min");
    expect(RefreshSchedule::retryDelayMs(5U) == 30U * kMinuteMs, "then every 30 min");
    expect(RefreshSchedule::retryDelayMs(99U) == 30U * kMinuteMs, "and stays there");
}

void testFirstRefreshIsDueAtOnce()
{
    RefreshSchedule::Scheduler scheduler;
    expect(scheduler.due({false, 0U, 1000U}), "power-up, clock unset: due");
    expect(scheduler.due(clock(at(23, 14, 0), 1000U)), "reset, clock kept, no success: due");
}

void testSuccessCoversItsSlot()
{
    RefreshSchedule::Scheduler scheduler;
    scheduler.recordOutcome(clock(at(23, 12, 11), 0U), true, true);
    expect(!scheduler.due(clock(at(23, 12, 12), 0U)), "just refreshed");
    expect(!scheduler.due(clock(at(23, 18, 9, 59), 0U)), "until the next slot");
    expect(scheduler.due(clock(at(23, 18, 10), 0U)), "the next slot is due");
    expect(scheduler.hasSuccess() && scheduler.lastSuccess() == at(23, 12, 11), "success kept");
}

void testManualRefreshCountsForTheSlot()
{
    RefreshSchedule::Scheduler scheduler;
    scheduler.recordOutcome(clock(at(23, 12, 11), 0U), true, true);
    // A console refresh at 18:05 happened before the 18:10 slot: it does not count.
    scheduler.recordOutcome(clock(at(23, 18, 5), 0U), true, true);
    expect(scheduler.due(clock(at(23, 18, 10), 0U)), "a refresh before the slot does not cover it");
    // One at 18:11 does, even though the scheduler did not start it.
    scheduler.recordOutcome(clock(at(23, 18, 11), 0U), true, true);
    expect(!scheduler.due(clock(at(23, 18, 12), 0U)), "a refresh in the slot covers it");
}

void testMissedSlotsAreCaughtUpOnce()
{
    RefreshSchedule::Scheduler scheduler;
    scheduler.restoreLastSuccess(at(22, 6, 11));
    expect(scheduler.due(clock(at(23, 9, 0), 0U)), "a day offline: due");
    scheduler.recordOutcome(clock(at(23, 9, 1), 0U), true, true);
    expect(!scheduler.due(clock(at(23, 9, 2), 0U)), "one refresh covers all missed slots");
}

void testFailuresBackOff()
{
    RefreshSchedule::Scheduler scheduler;
    const uint32_t start = 5000U;
    scheduler.recordOutcome(clock(at(23, 12, 11), start), false, true);
    expect(scheduler.failures() == 1U, "one failure");
    expect(!scheduler.due(clock(at(23, 12, 12), start + 1000U)), "waits after a failure");
    expect(scheduler.due(clock(at(23, 12, 13), start + 2U * kMinuteMs)), "retries after 2 min");
    const uint32_t second = start + 2U * kMinuteMs;
    scheduler.recordOutcome(clock(at(23, 12, 13), second), false, true);
    expect(!scheduler.due(clock(at(23, 12, 17), second + 4U * kMinuteMs)), "second retry waits 5 min");
    expect(scheduler.due(clock(at(23, 12, 18), second + 5U * kMinuteMs)), "then retries");
    scheduler.recordOutcome(clock(at(23, 12, 18), second + 5U * kMinuteMs), true, true);
    expect(scheduler.failures() == 0U, "a success clears the failures");
}

void testNextSlotEndsTheBackoff()
{
    RefreshSchedule::Scheduler scheduler;
    // Four failures: the next retry is 20 minutes away...
    for (uint32_t i = 0U; i < 4U; ++i) {
        scheduler.recordOutcome(clock(at(23, 18, 0), 0U), false, true);
    }
    expect(!scheduler.due(clock(at(23, 18, 9), 9U * kMinuteMs)), "still backing off");
    // ...but the 18:10 slot starts afresh.
    expect(scheduler.due(clock(at(23, 18, 10), 10U * kMinuteMs)), "a new slot does not wait");
}

void testTickWraps()
{
    RefreshSchedule::Scheduler scheduler;
    const uint32_t nearWrap = 0xFFFFFFFFU - kMinuteMs;
    scheduler.recordOutcome(clock(at(23, 12, 11), nearWrap), false, true);
    expect(!scheduler.due(clock(at(23, 12, 12), nearWrap + kMinuteMs)), "waits across the wrap");
    expect(scheduler.due(clock(at(23, 12, 13), nearWrap + 2U * kMinuteMs)), "due across the wrap");
}

void testNoCredentialsWaitsForTheNextSlot()
{
    RefreshSchedule::Scheduler scheduler;
    scheduler.recordOutcome(clock(at(23, 12, 11), 0U), false, false);
    expect(scheduler.waitingForNextSlot(), "gives up on the slot");
    expect(!scheduler.due(clock(at(23, 17, 0), 5U * 3600000U)), "no retries within it");
    expect(scheduler.due(clock(at(23, 18, 10), 6U * 3600000U)), "tries again in the next slot");
}

void testUnsetClock()
{
    RefreshSchedule::Scheduler scheduler;
    scheduler.recordOutcome({false, 0U, 0U}, false, true);
    expect(!scheduler.due({false, 0U, kMinuteMs}), "unset clock: backs off by ticks");
    expect(scheduler.due({false, 0U, 2U * kMinuteMs}), "unset clock: retries");
    RefreshSchedule::Scheduler noWifi;
    noWifi.recordOutcome({false, 0U, 0U}, false, false);
    expect(!noWifi.due({false, 0U, 24U * 3600000U}), "unset clock, no WiFi: waits for 'wifi set'");
}

void testClockSteps()
{
    RefreshSchedule::Scheduler scheduler;
    scheduler.recordOutcome(clock(at(23, 12, 11), 0U), true, true);
    // The next sync steps the RTC back a few seconds (or an hour, for DST):
    // the last success now looks like the future.
    expect(!scheduler.due(clock(at(23, 11, 11), 0U)), "a backward step does not refresh again");
    // A forward step past a slot is simply that slot.
    expect(scheduler.due(clock(at(23, 18, 30), 0U)), "a forward step past a slot refreshes");
}

} // namespace

int main()
{
    testSlots();
    testRetryDelays();
    testFirstRefreshIsDueAtOnce();
    testSuccessCoversItsSlot();
    testManualRefreshCountsForTheSlot();
    testMissedSlotsAreCaughtUpOnce();
    testFailuresBackOff();
    testNextSlotEndsTheBackoff();
    testTickWraps();
    testNoCredentialsWaitsForTheNextSlot();
    testUnsetClock();
    testClockSteps();

    if (failures != 0) {
        std::cerr << failures << " RefreshSchedule test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "RefreshSchedule tests passed\n";
    return EXIT_SUCCESS;
}
