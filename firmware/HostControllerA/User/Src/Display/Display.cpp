#include <Display/Display.hpp>

#include <Display/BootScreens.hpp>
#include <Display/DisplayI2cProtocol.hpp>

#include "cmsis_os2.h"

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
            noteSubmitted(position);
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
    noteSubmitted(position);
    return board->lastSubmitOk();
}

Display::Resend Display::resendRemote(uint8_t position, uint32_t nowMs)
{
    DisplayBoard* board = remoteBoard(position);
    if (board == nullptr) {
        return Resend::NothingToSend;
    }
    MutexGuard guard(submitMutex_);
    const uint32_t at = submittedAt_[position];
    if (at == 0U || static_cast<uint32_t>(nowMs - at) >= kContentLifetimeMs) {
        return Resend::NothingToSend;
    }
    board->submit();
    return board->lastSubmitOk() ? Resend::Sent : Resend::Failed;
}

void Display::noteSubmitted(uint8_t position)
{
    const uint32_t now = osKernelGetTickCount();
    // 0 means never; a submit in the tick's first millisecond counts as 1.
    submittedAt_[position] = (now == 0U) ? 1U : now;
}

void Display::runBootScreens()
{
    showBootScreens(local_, localAddress_);
    MutexGuard guard(submitMutex_);
    bootScreens_ = false;
    local_.submit();
}

} // namespace Display
