#pragma once

#include <cstdint>

namespace Console {

// Sends a long reply, such as 'errors' or 'help all', through the log queue
// without overflowing it. The console and log tasks share a priority, so a
// burst is queued before any of it is transmitted, and the queue (16 lines)
// drops lines when full; a short pause every few lines lets LogService drain
// it. Console task only: it blocks while pausing.
class PacedOutput
{
public:
    void line(const char* text);

private:
    static constexpr uint8_t kLinesPerBurst = 8U;
    static constexpr uint32_t kBurstPauseMs = 30U;

    uint8_t sent_ = 0U;
};

} // namespace Console
