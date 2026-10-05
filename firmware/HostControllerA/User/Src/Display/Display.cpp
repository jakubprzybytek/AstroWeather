#include <Display/Display.hpp>

#include <Display/BootScreens.hpp>

namespace Display {

void Display::submit()
{
    MutexGuard guard(submitMutex_);
    if (!bootScreens_) {
        local_.submit();
    }
    for (uint8_t position = 0U; position < kChainLength; ++position) {
        DisplayBoard* board = remoteBoard(position);
        if (board != nullptr) {
            board->submit();
        }
    }
}

void Display::submitLocal()
{
    MutexGuard guard(submitMutex_);
    if (!bootScreens_) {
        local_.submit();
    }
}

bool Display::submitRemote(uint8_t position)
{
    DisplayBoard* board = remoteBoard(position);
    if (board == nullptr) {
        return false;
    }
    MutexGuard guard(submitMutex_);
    board->submit();
    return board->lastSubmitOk();
}

void Display::runBootScreens()
{
    showBootScreens(local_, localAddress_);
    MutexGuard guard(submitMutex_);
    bootScreens_ = false;
    local_.submit();
}

} // namespace Display
