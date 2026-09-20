#pragma once

#include "main.h"

#include <cstdint>

namespace Device {

// Microchip 24AA01T-I/OT: 1 Kbit (128 x 8) I2C serial EEPROM in SOT-23-5.
//
// The part has no address pins, so the control byte is always 1010 000 x and
// only one device of this family can sit on the bus. The memory is byte
// addressable for reads; writes are limited to an 8-byte page and must be
// followed by the internal write cycle (t_WC, 5 ms max) before the device
// acknowledges again.
class Eeprom24AA01
{
public:
    static constexpr uint16_t kDeviceAddress = 0x50U;  // 7-bit, address bits not bonded
    static constexpr uint16_t kSize = 128U;            // bytes
    static constexpr uint16_t kPageSize = 8U;          // bytes per page write
    // Generous on purpose: with only the MCU's internal pull-ups the bus runs at
    // a few kHz, where a single 9-bit ack poll already costs more than 1 ms and a
    // full-array read costs tens of ms. These bound failures, they are not delays.
    static constexpr uint32_t kTransferTimeoutMs = 200U;
    static constexpr uint32_t kAckPollTimeoutMs = 10U;
    static constexpr uint32_t kWriteCycleTimeoutMs = 20U;  // t_WC is 5 ms max

    explicit Eeprom24AA01(I2C_HandleTypeDef& bus, uint16_t address = kDeviceAddress);

    // Acknowledge poll; HAL_OK means the chip is present and idle.
    HAL_StatusTypeDef probe(uint32_t trials = 3U);

    // Sequential read across the whole array; offset + size must stay within kSize.
    HAL_StatusTypeDef read(uint16_t offset, uint8_t* data, uint16_t size);

    // Page-aware write; splits the payload on page boundaries and waits out the
    // internal write cycle after every page.
    HAL_StatusTypeDef write(uint16_t offset, const uint8_t* data, uint16_t size);

    HAL_StatusTypeDef readByte(uint16_t offset, uint8_t& value);
    HAL_StatusTypeDef writeByte(uint16_t offset, uint8_t value);

    uint16_t address() const { return address_; }

    // Exposed so bring-up code can ack-poll the rest of the bus.
    I2C_HandleTypeDef& bus() const { return bus_; }
    HAL_StatusTypeDef lastStatus() const { return lastStatus_; }

private:
    HAL_StatusTypeDef writePage(uint16_t offset, const uint8_t* data, uint16_t size);
    HAL_StatusTypeDef waitForWriteCycle();
    HAL_StatusTypeDef fail(HAL_StatusTypeDef status);

    I2C_HandleTypeDef& bus_;
    uint16_t address_;
    HAL_StatusTypeDef lastStatus_ = HAL_OK;
};

} // namespace Device
