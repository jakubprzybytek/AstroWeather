#include <Display/Display.hpp>

namespace Display {

void Display::submit()
{
    MutexGuard guard(submitMutex_);
    local_.submit();
    for (DisplayBoard* board : remote_) {
        if (board != nullptr) {
            //board->submit();
        }
    }
}

} // namespace Display
