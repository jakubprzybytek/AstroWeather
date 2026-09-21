#pragma once

#include <Device/I2cBus.hpp>

#include "main.h"

#include <cstdint>

namespace Device {

// Microchip 24AA04HT-I/OT: 4 Kbit (512 x 8) I2C serial EEPROM in SOT-23-5,
// datasheet DS20002119.
//
// The array is two 256-byte blocks. The control byte is 1010 x x B0, where the
// x bits are don't-care (the SOT-23 package has no address pins) and B0 selects
// the block, so the part answers on all of 0x50-0x57: even addresses reach block
// 0 and odd addresses block 1. The word address byte then selects within the
// block. Only one device of this family can sit on the bus.
//
// Reads are sequential across the whole array, including across the block
// boundary. Writes are limited to a 16-byte page - a write crossing a page
// boundary wraps to the start of that page rather than continuing - and must
// be followed by the internal write cycle (t_WC, 5 ms max) before the device
// acknowledges again.
//
// The "H" variant's WP pin protects the upper block (100h-1FFh) when high. On
// this board it is tied to ground, so the whole array is writable.
class Eeprom24AA04
{
public:
    static constexpr uint16_t kDeviceAddress = 0x50U;  // 7-bit, block 0
    static constexpr uint16_t kSize = 512U;            // bytes
    static constexpr uint16_t kBlockSize = 256U;       // bytes per B0-selected block
    static constexpr uint16_t kPageSize = 16U;         // bytes per page write
    static_assert((kBlockSize % kPageSize) == 0U, "a page must never straddle a block");

    // Bounds on failure, not delays. A full 512-byte read is ~47 ms at 100 kHz,
    // so the transfer bound covers the largest single read with margin; the ack
    // poll bound also leaves room for the bus to be run slower than 100 kHz.
    static constexpr uint32_t kTransferTimeoutMs = 200U;
    static constexpr uint32_t kAckPollTimeoutMs = 10U;
    static constexpr uint32_t kWriteCycleTimeoutMs = 20U;  // t_WC is 5 ms max

    explicit Eeprom24AA04(I2cBus& bus, uint16_t address = kDeviceAddress);

    // Acknowledge poll; HAL_OK means the chip is present and idle.
    HAL_StatusTypeDef probe(uint32_t trials = 3U);

    // Sequential read; may span the block boundary. offset + size must stay
    // within kSize.
    HAL_StatusTypeDef read(uint16_t offset, uint8_t* data, uint16_t size);

    // Page-aware write; splits the payload on page boundaries and waits out the
    // internal write cycle after every page.
    HAL_StatusTypeDef write(uint16_t offset, const uint8_t* data, uint16_t size);

    HAL_StatusTypeDef readByte(uint16_t offset, uint8_t& value);
    HAL_StatusTypeDef writeByte(uint16_t offset, uint8_t value);

    uint16_t address() const { return address_; }

    // Exposed so bring-up code can ack-poll the rest of the bus.
    I2cBus& bus() const { return bus_; }
    HAL_StatusTypeDef lastStatus() const { return lastStatus_; }

private:
    // B0 of the control byte carries bit 8 of the array offset.
    uint16_t deviceAddressFor(uint16_t offset) const
    {
        return static_cast<uint16_t>(address_ | ((offset / kBlockSize) & 0x01U));
    }

    HAL_StatusTypeDef writePage(uint16_t offset, const uint8_t* data, uint16_t size);
    HAL_StatusTypeDef waitForWriteCycle();
    HAL_StatusTypeDef fail(HAL_StatusTypeDef status);

    I2cBus& bus_;
    uint16_t address_;
    HAL_StatusTypeDef lastStatus_ = HAL_OK;
};

} // namespace Device
