#include <Display/Display.hpp>

namespace Display {

void Display::submit()
{
    MutexGuard guard(submitMutex_);
    local_.submit();
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
    local_.submit();
}

} // namespace Display
