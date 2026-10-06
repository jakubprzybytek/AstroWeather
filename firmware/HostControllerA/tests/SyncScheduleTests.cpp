#include <Display/SyncSchedule.hpp>

#include <Expect.hpp>

// When the host sends the timeline sync and polls the boards.

namespace {

using Test::expect;
using Test::expectEqual;
using Display::SyncSchedule;

// Runs the schedule from `start` for `ms`, sending whatever is due, and
// returns the send times relative to `start`.
std::size_t run(SyncSchedule& schedule, uint32_t start, uint32_t ms, uint32_t* sent,
                std::size_t capacity)
{
    std::size_t count = 0U;
    uint32_t now = start;
    while (static_cast<uint32_t>(now - start) <= ms) {
        if (schedule.syncDue(now)) {
            if (count < capacity) {
                sent[count] = now - start;
            }
            ++count;
            schedule.onSyncSent(now);
        }
        if (schedule.pollDue(now)) {
            schedule.onPolled(now);
        }
        const uint32_t wait = schedule.waitMs(now);
        now += (wait == 0U) ? 1U : wait;
    }
    return count;
}

void testBootBurstThenEveryFiveMinutes()
{
    SyncSchedule schedule;
    schedule.start(1000U);
    expect(schedule.inBurst(), "boot starts a burst");
    uint32_t sent[16] = {};
    const std::size_t count = run(schedule, 1000U, 960000U, sent, 16U);
    expectEqual(count, static_cast<std::size_t>(7), "four in the burst, three after");
    expectEqual(sent[0], 0U, "at once");
    expectEqual(sent[1], 10000U, "10 s");
    expectEqual(sent[2], 30000U, "30 s");
    expectEqual(sent[3], 60000U, "60 s");
    expectEqual(sent[4], 360000U, "then 5 min after the last");
    expectEqual(sent[5], 660000U, "every 5 min");
    expect(!schedule.inBurst(), "burst over");
}

void testPolls()
{
    SyncSchedule schedule;
    schedule.start(0U);
    expect(!schedule.pollDue(14999U), "no poll before 15 s");
    expect(schedule.pollDue(15000U), "first poll at 15 s");
    schedule.onPolled(15000U);
    expect(!schedule.pollDue(74999U), "a minute between polls");
    expect(schedule.pollDue(75000U), "next poll");
}

void testRequestedBurst()
{
    SyncSchedule schedule;
    schedule.start(0U);
    expect(!schedule.requestBurst(5000U), "ignored during the boot burst");
    uint32_t sent[8] = {};
    run(schedule, 0U, 100000U, sent, 8U);
    expect(schedule.requestBurst(100000U), "accepted after it");
    expect(schedule.syncDue(100000U), "a sync at once");
    schedule.onSyncSent(100000U);
    expectEqual(schedule.untilSyncMs(100000U), 10000U, "then the burst's 10 s");
}

void testTickWrap()
{
    SyncSchedule schedule;
    const uint32_t start = 0xFFFFF000U;
    schedule.start(start);
    uint32_t sent[8] = {};
    const std::size_t count = run(schedule, start, 61000U, sent, 8U);
    expectEqual(count, static_cast<std::size_t>(4), "the burst across the tick wrap");
    expectEqual(sent[3], 60000U, "on time");
}

} // namespace

int main()
{
    testBootBurstThenEveryFiveMinutes();
    testPolls();
    testRequestedBurst();
    testTickWrap();
    return Test::finish("SyncSchedule");
}
