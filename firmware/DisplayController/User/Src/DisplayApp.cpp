#include <DisplayApp.hpp>

#include <Display/BootScreens.hpp>
#include <Screens.hpp>
#include <Stats.hpp>

DisplayApp::DisplayApp(Display::DisplayBoard& board, I2cTarget& link, TimelineFollower& timeline)
    : Task<2048>("DisplayApp", osPriorityNormal), board_(board), link_(link), timeline_(timeline)
{
}

void DisplayApp::run()
{
    // Boot: each slot lit on its own (spot a dead segment or slot switch),
    // then this board's address. Frames and switch presses arriving meanwhile
    // stay pending in the thread flags.
    Display::showBootScreens(board_, link_.address());
    show();

    for (;;) {
        const uint32_t flags = osThreadFlagsWait(
            kFlagFrame | kFlagSwitch1 | kFlagSwitch2 | kFlagSync, osFlagsWaitAny, kPollMs);
        const uint32_t now = osKernelGetTickCount();
        bool changed = false;

        if ((flags & osFlagsError) == 0U) {
            if ((flags & kFlagSync) != 0U) {
                I2cTarget::Sync sync{};
                if (link_.takeSync(sync)) {
                    timeline_.onSync(sync);
                }
            }
            if ((flags & kFlagFrame) != 0U) {
                changed = receiveFrames(now) || changed;
            }
            if ((flags & kFlagSwitch1) != 0U) {
                // Switch 1 steps through the test screens and back to the data.
                const Screen next = (screen_ == Screen::AllSegments) ? Screen::Identify
                                    : (screen_ == Screen::Identify)  ? Screen::Data
                                                                     : Screen::AllSegments;
                setScreen(next, now);
                changed = true;
            }
            if ((flags & kFlagSwitch2) != 0U) {
                setScreen(Screen::Address, now);
                changed = true;
            }
        }

        if (noData_.expire(now)) {
            ++g_displayStats.staleTimeouts;
            // The host may hold newer content that did not reach this board.
            link_.setNeedsContent(true);
            changed = true;
        }
        if (screenTimedOut(now)) {
            setScreen(Screen::Data, now);
            changed = true;
        }
        link_.ensureListening();
        publishRefreshStats();

        if (changed) {
            show();
        }
    }
}

// Takes every message waiting on the link. Attributes are staged, content
// applies them; a frame received while a test screen is up is kept and
// shown when the screen closes.
bool DisplayApp::receiveFrames(uint32_t now)
{
    bool newContent = false;
    Display::I2cMessage message{};
    while (link_.takeMessage(message)) {
        switch (frames_.accept(message.data(), message.size())) {
        case DisplayController::FrameAssembler::Result::Content:
            newContent = true;
            noData_.onFrame(now);
            link_.setNeedsContent(false);
            ++g_displayStats.framesAccepted;
            g_displayStats.lastFrameTick = now;
            break;
        case DisplayController::FrameAssembler::Result::Staged:
            ++g_displayStats.attributesAccepted;
            break;
        case DisplayController::FrameAssembler::Result::Rejected:
        default:
            ++g_displayStats.framesRejected;
            break;
        }
    }
    return newContent && screen_ == Screen::Data;
}

void DisplayApp::publishRefreshStats()
{
    Display::DisplayBoard::RefreshStats stats{};
    if (board_.refreshStats(stats)) {
        g_displayStats.refreshFrames = stats.frames;
        g_displayStats.lateShifts = stats.lateShifts;
        g_displayStats.lateInterrupts = stats.lateInterrupts;
        g_displayStats.maxInterruptMicros = stats.maxInterruptMicros;
    }
}

void DisplayApp::setScreen(Screen screen, uint32_t now)
{
    screen_ = screen;
    screenSince_ = now;
}

bool DisplayApp::screenTimedOut(uint32_t now) const
{
    const uint32_t elapsed = DisplayController::elapsedMs(now, screenSince_);
    switch (screen_) {
    case Screen::Address:
        return elapsed >= kAddressScreenMs;
    case Screen::AllSegments:
    case Screen::Identify:
        return elapsed >= kTestScreenMs;
    case Screen::Data:
    default:
        return false;
    }
}

void DisplayApp::show()
{
    // The test screens and "no data" are plain: full brightness, no blink.
    Display::BoardAttributes attributes{};
    switch (screen_) {
    case Screen::AllSegments:
        board_.setState(DisplayController::allSegmentsState());
        break;
    case Screen::Identify:
        board_.setState(DisplayController::identifyState());
        break;
    case Screen::Address:
        board_.setState(Display::addressState(link_.address()));
        break;
    case Screen::Data:
    default:
        if (noData_.hasData()) {
            board_.setState(frames_.content());
            attributes = frames_.attributes();
        } else {
            board_.setState(Display::noDataState());
        }
        break;
    }
    board_.setAttributes(attributes);
    board_.submit();
}
