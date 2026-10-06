#include <Console/PacedOutput.hpp>

#include <Debug/LogService.hpp>

#include "cmsis_os2.h"

namespace Console {

void PacedOutput::line(const char* text)
{
    LogService::instance().sendLine(text);
    if (++sent_ % kLinesPerBurst == 0U) {
        osDelay(kBurstPauseMs);
    }
}

} // namespace Console
