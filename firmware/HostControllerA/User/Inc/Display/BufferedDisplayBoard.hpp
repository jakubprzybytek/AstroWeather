#pragma once

#include <Display/DisplayBoard.hpp>
#include <Device/I2cBus.hpp>

#include "main.h"

namespace Display {

class BufferedDisplayBoard : public DisplayBoard {
public:
    BufferedDisplayBoard(Device::I2cBus& bus, uint16_t address)
        : bus_(bus), address_(address) {}

    void submit() override;
    uint16_t address() const override { return address_; }
    // Probes the bus now, independent of the last submit(): a board that has
    // not been refreshed since boot would otherwise look reachable.
    bool present() override;
    bool lastSubmitOk() const override { return online(); }
    HAL_StatusTypeDef lastStatus() const { return lastStatus_; }
    bool online() const { return lastStatus_ == HAL_OK; }

private:
    // A refresh is ~24 bytes, so ~2.5 ms at 100 kHz. This was HAL_MAX_DELAY,
    // which hung the calling task outright when the bus could not complete a
    // transfer - the reason remote submits were commented out.
    static constexpr uint32_t kTransferTimeoutMs = 50U;
    static constexpr uint32_t kProbeTimeoutMs = 5U;

    // How often to restate that a board is still unreachable (tick rate is 1 kHz).
    static constexpr uint32_t kReportIntervalMs = 30000U;

    // A board that still answers its address after a failed submit gets the
    // whole submit again, up to this many times in all, kRetryDelayMs apart.
    // An absent board fails the probe and is not retried.
    static constexpr uint8_t kSubmitAttempts = 3U;
    static constexpr uint32_t kRetryDelayMs = 5U;

    // One attempt: the attributes, then the content. HAL_OK, or the first
    // failure with its HAL error bits in `errorCode`.
    HAL_StatusTypeDef send(uint32_t& errorCode);
    void report(HAL_StatusTypeDef status, uint32_t errorCode, uint8_t attempts);

    Device::I2cBus& bus_;
    uint16_t address_;
    HAL_StatusTypeDef lastStatus_ = HAL_OK;
    uint32_t lastReportTick_ = 0U;
};

} // namespace Display
