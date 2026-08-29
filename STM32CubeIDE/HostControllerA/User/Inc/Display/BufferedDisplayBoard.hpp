#pragma once

#include <Display/DisplayBoard.hpp>

#include "main.h"

namespace Display {

class BufferedDisplayBoard : public DisplayBoard {
public:
    BufferedDisplayBoard(I2C_HandleTypeDef& bus, uint16_t address)
        : bus_(bus), address_(address) {}

    void submit() override;
    uint16_t address() const { return address_; }
    HAL_StatusTypeDef lastStatus() const { return lastStatus_; }

private:
    I2C_HandleTypeDef& bus_;
    uint16_t address_;
    HAL_StatusTypeDef lastStatus_ = HAL_OK;
};

} // namespace Display
