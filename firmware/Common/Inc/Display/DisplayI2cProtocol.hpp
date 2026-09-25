#pragma once

#include <Display/DisplayTypes.hpp>

#include <cstddef>
#include <cstdint>

namespace Display {

constexpr uint8_t kSetDisplayCommand = 0x01U;

void serializeI2c(const LogicalBoardState& state, I2cMessage& message);
bool deserializeI2c(const uint8_t* data, std::size_t size, LogicalBoardState& destination);

} // namespace Display
