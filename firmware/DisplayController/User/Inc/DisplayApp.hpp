#pragma once

#include <Display/DisplayBoard.hpp>
#include <I2cTarget.hpp>
#include <NoDataTimer.hpp>
#include <Utils/Led.hpp>
#include <Utils/Task.hpp>

#include <cstdint>

// Decides what the board shows: the boot screens, the host's data, "no data"
// before the first frame and after the data goes stale, and the switch-driven
// test screens. Owns no hardware; the refresh itself is PcbDisplayBoard's task.
class DisplayApp : public Task<1024> {
public:
    static constexpr uint32_t kFlagFrame = 1U << 0;
    static constexpr uint32_t kFlagSwitch1 = 1U << 1;
    static constexpr uint32_t kFlagSwitch2 = 1U << 2;

    // Longer than the host's 6-hour refresh interval: the host sends nothing
    // between refreshes, so a shorter timeout would show "no data" most of
    // the day.
    static constexpr uint32_t kNoDataTimeoutMs = 7UL * 60UL * 60UL * 1000UL;

    DisplayApp(Display::DisplayBoard& board, I2cTarget& link, Led& activityLed);

protected:
    void run() override;

private:
    enum class Screen : uint8_t { Data, AllSegments, Identify, Address };

    static constexpr uint32_t kSelfTestMs = 1000U;
    static constexpr uint32_t kBootAddressMs = 2000U;
    static constexpr uint32_t kAddressScreenMs = 3000U;
    static constexpr uint32_t kTestScreenMs = 60000U;
    static constexpr uint32_t kPollMs = 1000U;
    static constexpr uint32_t kActivityBlinkMs = 20U;

    bool receiveFrames(uint32_t now);
    void setScreen(Screen screen, uint32_t now);
    bool screenTimedOut(uint32_t now) const;
    void show();

    Display::DisplayBoard& board_;
    I2cTarget& link_;
    Led& activityLed_;
    DisplayController::NoDataTimer noData_{kNoDataTimeoutMs};
    Display::LogicalBoardState data_{};
    Screen screen_ = Screen::Data;
    uint32_t screenSince_ = 0U;
};
