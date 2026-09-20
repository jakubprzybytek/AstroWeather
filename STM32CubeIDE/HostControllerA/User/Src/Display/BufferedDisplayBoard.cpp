#include <Display/BufferedDisplayBoard.hpp>
#include <Display/DisplayI2cProtocol.hpp>

#include <Debug/LogService.hpp>

#include "cmsis_os2.h"

namespace Display {

void BufferedDisplayBoard::submit()
{
    I2cMessage message{};
    serializeI2c(state_, message);
    if (address_ < 0x10U || address_ > 0x2AU) {
        report(HAL_ERROR);
        return;
    }
    report(HAL_I2C_Master_Transmit(&bus_, static_cast<uint16_t>(address_ << 1U),
                                   message.data(), static_cast<uint16_t>(message.size()),
                                   kTransferTimeoutMs));
}

void BufferedDisplayBoard::report(HAL_StatusTypeDef status)
{
    const bool nowOnline = (status == HAL_OK);
    const bool wasOnline = (lastStatus_ == HAL_OK);
    lastStatus_ = status;

    if (nowOnline) {
        if (!wasOnline) {
            LogService::instance().logf(LogService::Level::Info,
                                        "DisplayBoard 0x%02X online",
                                        static_cast<unsigned>(address_));
        }
        return;
    }

    // Refreshes run at 10 Hz, so an absent board cannot be logged every time.
    // Logging only the edge does not work either: a board missing from boot
    // fails before USB CDC has enumerated, so that one message is dropped and
    // never repeated. Restate it on a slow interval instead.
    const uint32_t now = osKernelGetTickCount();
    if (wasOnline || (now - lastReportTick_) >= kReportIntervalMs) {
        LogService::instance().logf(LogService::Level::Warn,
                                    "DisplayBoard 0x%02X unreachable status=%u",
                                    static_cast<unsigned>(address_),
                                    static_cast<unsigned>(status));
        lastReportTick_ = now;
    }
}

} // namespace Display
