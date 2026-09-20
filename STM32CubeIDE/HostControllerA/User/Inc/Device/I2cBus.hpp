#pragma once

#include <Utils/Mutex.hpp>

#include "main.h"

#include <cstdint>

namespace Device {

// Serializes access to one I2C peripheral.
//
// Several unrelated clients share hi2c1 - the settings EEPROM and every remote
// display board - and they are driven from different tasks. The HAL handle
// carries per-transfer state and guards itself only well enough to reject an
// overlapping call with HAL_BUSY, which turns contention into a transfer that
// intermittently fails rather than one that waits its turn.
//
// The lock covers a single transfer, not a whole logical operation. A multi-page
// EEPROM write is safe to have other traffic interleaved between its pages
// (its ack poll is addressed to the EEPROM, so other devices are irrelevant),
// and holding the bus across a full 16-page erase would stall display
// refreshes for ~80 ms for no benefit.
//
// Device addresses are 7-bit; the read/write bit is added here so call sites
// cannot get the shift wrong. Memory addresses are 8-bit (I2C_MEMADD_SIZE_8BIT).
class I2cBus
{
public:
    explicit I2cBus(I2C_HandleTypeDef& handle) : handle_(handle) {}

    HAL_StatusTypeDef transmit(uint16_t deviceAddress, const uint8_t* data, uint16_t size,
                               uint32_t timeoutMs);
    HAL_StatusTypeDef memRead(uint16_t deviceAddress, uint16_t memAddress, uint8_t* data,
                              uint16_t size, uint32_t timeoutMs);
    HAL_StatusTypeDef memWrite(uint16_t deviceAddress, uint16_t memAddress, const uint8_t* data,
                               uint16_t size, uint32_t timeoutMs);
    HAL_StatusTypeDef isDeviceReady(uint16_t deviceAddress, uint32_t trials, uint32_t timeoutMs);

    // The peripheral's own configuration, for callers that need to inspect it
    // rather than transfer on it.
    const I2C_HandleTypeDef& handle() const { return handle_; }

private:
    I2C_HandleTypeDef& handle_;
    Mutex mutex_;
};

} // namespace Device
