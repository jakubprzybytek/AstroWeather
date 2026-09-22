#include <Device/Eeprom24AA04.hpp>

#include "cmsis_os2.h"

namespace {

// The class is usable both from tasks and from AppVariant_Init(), which runs
// before osKernelStart(). Only block the scheduler-friendly way once it is up.
void delayMs(uint32_t milliseconds)
{
    if (osKernelGetState() == osKernelRunning) {
        osDelay(milliseconds);
    } else {
        HAL_Delay(milliseconds);
    }
}

} // namespace

namespace Device {

Eeprom24AA04::Eeprom24AA04(I2cBus& bus, uint16_t address)
    : bus_(bus), address_(address) {}

HAL_StatusTypeDef Eeprom24AA04::fail(HAL_StatusTypeDef status)
{
    lastStatus_ = status;
    return status;
}

HAL_StatusTypeDef Eeprom24AA04::probe(uint32_t trials)
{
    return fail(bus_.isDeviceReady(address_, trials, kTransferTimeoutMs));
}

HAL_StatusTypeDef Eeprom24AA04::read(uint16_t offset, uint8_t* data, uint16_t size)
{
    if (data == nullptr || size == 0U || offset >= kSize || size > static_cast<uint16_t>(kSize - offset)) {
        return fail(HAL_ERROR);
    }
    // One transaction even across the block boundary: the datasheet's address
    // pointer runs through the entire array, so only the starting block needs
    // selecting.
    return fail(bus_.memRead(deviceAddressFor(offset), static_cast<uint16_t>(offset & 0xFFU),
                             data, size, kTransferTimeoutMs));
}

HAL_StatusTypeDef Eeprom24AA04::write(uint16_t offset, const uint8_t* data, uint16_t size)
{
    if (data == nullptr || size == 0U || offset >= kSize || size > static_cast<uint16_t>(kSize - offset)) {
        return fail(HAL_ERROR);
    }

    uint16_t written = 0U;
    while (written < size) {
        const uint16_t pageStart = static_cast<uint16_t>(offset + written);
        const uint16_t roomInPage = static_cast<uint16_t>(kPageSize - (pageStart % kPageSize));
        const uint16_t remaining = static_cast<uint16_t>(size - written);
        const uint16_t chunk = (remaining < roomInPage) ? remaining : roomInPage;

        const HAL_StatusTypeDef status = writePage(pageStart, &data[written], chunk);
        if (status != HAL_OK) {
            return status;
        }
        written = static_cast<uint16_t>(written + chunk);
    }
    return fail(HAL_OK);
}

HAL_StatusTypeDef Eeprom24AA04::writePage(uint16_t offset, const uint8_t* data, uint16_t size)
{
    // write() never hands over a chunk crossing a page, and pages never cross a
    // block, so the starting offset's block holds the whole chunk.
    const HAL_StatusTypeDef status =
        bus_.memWrite(deviceAddressFor(offset), static_cast<uint16_t>(offset & 0xFFU), data, size,
                      kTransferTimeoutMs);
    if (status != HAL_OK) {
        return fail(status);
    }
    return waitForWriteCycle();
}

HAL_StatusTypeDef Eeprom24AA04::waitForWriteCycle()
{
    // The device NACKs its control byte until the internal write cycle finishes.
    // Each poll is a full address transfer, so it needs a timeout of its own that
    // survives a slow bus; 2 ms here silently failed mid-erase at ~6.6 kHz.
    for (uint32_t attempt = 0U; attempt < kWriteCycleTimeoutMs; ++attempt) {
        delayMs(1U);
        if (bus_.isDeviceReady(address_, 1U, kAckPollTimeoutMs) == HAL_OK) {
            return fail(HAL_OK);
        }
    }
    return fail(HAL_TIMEOUT);
}

HAL_StatusTypeDef Eeprom24AA04::readByte(uint16_t offset, uint8_t& value)
{
    return read(offset, &value, 1U);
}

HAL_StatusTypeDef Eeprom24AA04::writeByte(uint16_t offset, uint8_t value)
{
    return write(offset, &value, 1U);
}

} // namespace Device
