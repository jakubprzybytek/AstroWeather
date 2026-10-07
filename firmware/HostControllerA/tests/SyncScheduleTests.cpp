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
    const std::size_t count = run(schedule, 1000U, 500000U, sent, 16U);
    expectEqual(count, static_cast<std::size_t>(7), "four in the burst, three after");
    expectEqual(sent[0], 0U, "at once");
    expectEqual(sent[1], 10000U, "10 s");
    expectEqual(sent[2], 40000U, "40 s");
    expectEqual(sent[3], 100000U, "100 s");
    expectEqual(sent[4], 220000U, "then 2 min after the last");
    expectEqual(sent[5], 340000U, "every 2 min");
    expect(!schedule.inBurst(), "burst over");
}

// A board asks for syncs at the poll after a sync that jumped its timeline
// or stepped its HSITRIM: that sync is the burst's first.
void testBurstAfterASync()
{
    SyncSchedule schedule;
    schedule.start(0U);
    uint32_t sent[8] = {};
    run(schedule, 0U, 220000U, sent, 8U);
    expectEqual(sent[4], 220000U, "the first 2-minute sync");
    expect(schedule.requestBurst(220005U), "a burst 5 ms after it");
    expect(schedule.inBurst(), "in the burst");
    expect(!schedule.syncDue(220005U), "no second sync now");
    expectEqual(schedule.untilSyncMs(220005U), 9995U, "the burst's 10 s from that sync");
    schedule.onSyncSent(230000U);
    expectEqual(schedule.untilSyncMs(230000U), 30000U, "then its 40 s");
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
    expect(schedule.requestBurst(150000U), "accepted after it");
    expect(schedule.syncDue(150000U), "a sync at once");
    schedule.onSyncSent(150000U);
    expectEqual(schedule.untilSyncMs(150000U), 10000U, "then the burst's 10 s");
}

void testTickWrap()
{
    SyncSchedule schedule;
    const uint32_t start = 0xFFFFF000U;
    schedule.start(start);
    uint32_t sent[8] = {};
    const std::size_t count = run(schedule, start, 101000U, sent, 8U);
    expectEqual(count, static_cast<std::size_t>(4), "the burst across the tick wrap");
    expectEqual(sent[3], 100000U, "on time");
}

} // namespace

int main()
{
    testBootBurstThenEveryFiveMinutes();
    testBurstAfterASync();
    testPolls();
    testRequestedBurst();
    testTickWrap();
    return Test::finish("SyncSchedule");
}
