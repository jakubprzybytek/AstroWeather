#include <Display/BufferedDisplayBoard.hpp>
#include <Display/DisplayI2cProtocol.hpp>

#include <Debug/LogService.hpp>

#include "cmsis_os2.h"

namespace Display {

void BufferedDisplayBoard::submit()
{
    if (address_ < 0x10U || address_ > 0x2AU) {
        report(HAL_ERROR, 0U, 1U);
        return;
    }
    // A transfer to a board that is there can still fail: noise on the
    // cable, or this task kept off the CPU past the transfer timeout (the
    // HAL times the whole transfer). Without a retry the board would wait
    // for the next refresh, up to 6 hours, so try again while it answers.
    uint32_t firstError = 0U;
    HAL_StatusTypeDef status = HAL_ERROR;
    uint8_t attempt = 0U;
    while (attempt < kSubmitAttempts) {
        ++attempt;
        uint32_t errorCode = 0U;
        status = send(errorCode);
        if (status == HAL_OK) {
            break;
        }
        if (attempt == 1U) {
            firstError = errorCode;
        }
        if (attempt == kSubmitAttempts || !present()) {
            break;
        }
        osDelay(kRetryDelayMs);
    }
    report(status, firstError, attempt);
}

HAL_StatusTypeDef BufferedDisplayBoard::send(uint32_t& errorCode)
{
    // The attributes first, which the board only stages, then the content,
    // which applies them; a board that misses the attributes shows the
    // content at full brightness. Stop at the first failure: the rest would
    // fail the same way.
    AttributeMessages attributes{};
    serializeAttributesI2c(attributes_, attributes);
    for (const I2cMessage& message : attributes) {
        const HAL_StatusTypeDef status =
            bus_.transmit(address_, message.data(), static_cast<uint16_t>(message.size()),
                          kTransferTimeoutMs, &errorCode);
        if (status != HAL_OK) {
            return status;
        }
    }
    I2cMessage content{};
    serializeI2c(state_, content);
    return bus_.transmit(address_, content.data(), static_cast<uint16_t>(content.size()),
                         kTransferTimeoutMs, &errorCode);
}

bool BufferedDisplayBoard::present()
{
    return bus_.isDeviceReady(address_, 1U, kProbeTimeoutMs) == HAL_OK;
}

void BufferedDisplayBoard::report(HAL_StatusTypeDef status, uint32_t errorCode, uint8_t attempts)
{
    const bool nowOnline = (status == HAL_OK);
    const bool wasOnline = (lastStatus_ == HAL_OK);
    lastStatus_ = status;

    if (nowOnline) {
        if (attempts > 1U) {
            // Kept in 'errors': which error it was says whether it is the
            // cable or the timing.
            LogService::instance().logf(LogService::Level::Warn,
                                        "DisplayBoard 0x%02X sent on attempt %u, first error=0x%lX",
                                        static_cast<unsigned>(address_),
                                        static_cast<unsigned>(attempts),
                                        static_cast<unsigned long>(errorCode));
        } else if (!wasOnline) {
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
                                    "DisplayBoard 0x%02X unreachable status=%u error=0x%lX attempts=%u",
                                    static_cast<unsigned>(address_),
                                    static_cast<unsigned>(status),
                                    static_cast<unsigned long>(errorCode),
                                    static_cast<unsigned>(attempts));
        lastReportTick_ = now;
    }
}

} // namespace Display
