#include <Display/Display.hpp>

namespace Display {

void Display::submit()
{
    local_.submit();
    for (DisplayBoard* board : remote_) {
        if (board != nullptr) {
            //board->submit();
        }
    }
}

} // namespace Display
