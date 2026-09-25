#pragma once

#include <cstdint>

namespace DisplayController {

// Milliseconds from `since` to `now` on the free-running 32-bit kernel tick;
// correct across its wrap.
constexpr uint32_t elapsedMs(uint32_t now, uint32_t since) { return now - since; }

// Tracks whether the board holds data from the host that is still current.
// The host sends only when it has something new (an astro refresh every
// 6 hours, or a console 'display' command), so the timeout must be longer than
// one refresh interval; see docs/Architecture.md.
class NoDataTimer {
public:
    explicit constexpr NoDataTimer(uint32_t timeoutMs) : timeoutMs_(timeoutMs) {}

    void onFrame(uint32_t now)
    {
        lastFrame_ = now;
        hasData_ = true;
    }

    // True exactly once, when the data has just gone stale; hasData() is
    // false from then until the next frame.
    bool expire(uint32_t now)
    {
        if (hasData_ && elapsedMs(now, lastFrame_) >= timeoutMs_) {
            hasData_ = false;
            return true;
        }
        return false;
    }

    bool hasData() const { return hasData_; }

private:
    uint32_t timeoutMs_;
    uint32_t lastFrame_ = 0U;
    bool hasData_ = false;
};

} // namespace DisplayController
