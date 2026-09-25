#include <DisplayApp.hpp>

#include <Display/DisplayI2cProtocol.hpp>
#include <Screens.hpp>
#include <Stats.hpp>

DisplayApp::DisplayApp(Display::DisplayBoard& board, I2cTarget& link, Led& activityLed)
    : Task<1024>("DisplayApp", osPriorityNormal), board_(board), link_(link),
      activityLed_(activityLed)
{
}

void DisplayApp::run()
{
    // Boot: every segment (spot dead ones), then this board's address. Frames
    // and switch presses arriving meanwhile stay pending in the thread flags.
    board_.setState(DisplayController::allSegmentsState());
    board_.submit();
    osDelay(kSelfTestMs);
    board_.setState(DisplayController::addressState(link_.address()));
    board_.submit();
    osDelay(kBootAddressMs);
    show();

    for (;;) {
        const uint32_t flags = osThreadFlagsWait(kFlagFrame | kFlagSwitch1 | kFlagSwitch2,
                                                 osFlagsWaitAny, kPollMs);
        const uint32_t now = osKernelGetTickCount();
        bool changed = false;

        if ((flags & osFlagsError) == 0U) {
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
            changed = true;
        }
        if (screenTimedOut(now)) {
            setScreen(Screen::Data, now);
            changed = true;
        }
        link_.ensureListening();

        if (changed) {
            show();
        }
    }
}

// Takes the newest message from the link. A frame received while a test
// screen is up is kept and shown when the screen closes.
bool DisplayApp::receiveFrames(uint32_t now)
{
    Display::I2cMessage message{};
    if (!link_.takeMessage(message)) {
        return false;
    }
    Display::LogicalBoardState decoded{};
    if (!Display::deserializeI2c(message.data(), message.size(), decoded)) {
        ++g_displayStats.framesRejected;
        return false;
    }
    data_ = decoded;
    noData_.onFrame(now);
    ++g_displayStats.framesAccepted;
    g_displayStats.lastFrameTick = now;
    activityLed_.blink(kActivityBlinkMs);
    return screen_ == Screen::Data;
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
    switch (screen_) {
    case Screen::AllSegments:
        board_.setState(DisplayController::allSegmentsState());
        break;
    case Screen::Identify:
        board_.setState(DisplayController::identifyState());
        break;
    case Screen::Address:
        board_.setState(DisplayController::addressState(link_.address()));
        break;
    case Screen::Data:
    default:
        board_.setState(noData_.hasData() ? data_ : Display::noDataState());
        break;
    }
    board_.submit();
}
