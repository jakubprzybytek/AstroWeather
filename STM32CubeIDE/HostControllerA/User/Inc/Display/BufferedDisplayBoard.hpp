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
    uint16_t address() const { return address_; }
    HAL_StatusTypeDef lastStatus() const { return lastStatus_; }
    bool online() const { return lastStatus_ == HAL_OK; }

private:
    // A refresh is ~24 bytes, so ~2.5 ms at 100 kHz. This was HAL_MAX_DELAY,
    // which hung the calling task outright when the bus could not complete a
    // transfer - the reason remote submits were commented out.
    static constexpr uint32_t kTransferTimeoutMs = 50U;

    // How often to restate that a board is still unreachable (tick rate is 1 kHz).
    static constexpr uint32_t kReportIntervalMs = 30000U;

    void report(HAL_StatusTypeDef status);

    Device::I2cBus& bus_;
    uint16_t address_;
    HAL_StatusTypeDef lastStatus_ = HAL_OK;
    uint32_t lastReportTick_ = 0U;
};

} // namespace Display
