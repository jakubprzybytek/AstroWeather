// When the host's data counts as stale (User/Inc/NoDataTimer.hpp).

#include <NoDataTimer.hpp>

#include <Expect.hpp>

#include <cstdint>

namespace {

using DisplayController::NoDataTimer;
using Test::expect;

constexpr uint32_t kTimeout = 7UL * 60UL * 60UL * 1000UL;

void testNoDataBeforeFirstFrame()
{
    NoDataTimer timer(kTimeout);
    expect(!timer.hasData(), "no data at boot");
    expect(!timer.expire(kTimeout * 2U), "nothing to expire before the first frame");
}

void testExpiresAfterTimeout()
{
    NoDataTimer timer(kTimeout);
    timer.onFrame(1000U);
    expect(timer.hasData(), "a frame gives data");
    expect(!timer.expire(1000U + kTimeout - 1U), "still current 1 ms before the timeout");
    expect(timer.hasData(), "data kept until the timeout");
    expect(timer.expire(1000U + kTimeout), "stale at the timeout");
    expect(!timer.hasData(), "no data once stale");
    expect(!timer.expire(1000U + kTimeout + 1U), "expires only once");
}

void testFrameRestartsTimeout()
{
    NoDataTimer timer(kTimeout);
    timer.onFrame(0U);
    timer.onFrame(6UL * 60UL * 60UL * 1000UL);
    expect(!timer.expire(kTimeout), "a refresh 6 h later keeps the data current");
    expect(timer.expire(6UL * 60UL * 60UL * 1000UL + kTimeout), "stale 7 h after the last frame");
}

void testFrameAfterStaleRestoresData()
{
    NoDataTimer timer(kTimeout);
    timer.onFrame(0U);
    timer.expire(kTimeout);
    timer.onFrame(kTimeout + 5U);
    expect(timer.hasData(), "a new frame after going stale gives data again");
}

void testTickWrap()
{
    NoDataTimer timer(kTimeout);
    const uint32_t beforeWrap = 0xFFFFFFFFU - 1000U;
    timer.onFrame(beforeWrap);
    expect(!timer.expire(5000U), "current across the tick wrap");
    expect(timer.expire(beforeWrap + kTimeout), "stale across the tick wrap");
}

} // namespace

int main()
{
    testNoDataBeforeFirstFrame();
    testExpiresAfterTimeout();
    testFrameRestartsTimeout();
    testFrameAfterStaleRestoresData();
    testTickWrap();
    return Test::finish("NoDataTimer");
}
