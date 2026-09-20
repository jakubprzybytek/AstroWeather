#include <Device/Eeprom24AA01.hpp>

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

Eeprom24AA01::Eeprom24AA01(I2cBus& bus, uint16_t address)
    : bus_(bus), address_(address) {}

HAL_StatusTypeDef Eeprom24AA01::fail(HAL_StatusTypeDef status)
{
    lastStatus_ = status;
    return status;
}

HAL_StatusTypeDef Eeprom24AA01::probe(uint32_t trials)
{
    return fail(bus_.isDeviceReady(address_, trials, kTransferTimeoutMs));
}

HAL_StatusTypeDef Eeprom24AA01::read(uint16_t offset, uint8_t* data, uint16_t size)
{
    if (data == nullptr || size == 0U || offset >= kSize || size > static_cast<uint16_t>(kSize - offset)) {
        return fail(HAL_ERROR);
    }
    return fail(bus_.memRead(address_, offset, data, size, kTransferTimeoutMs));
}

HAL_StatusTypeDef Eeprom24AA01::write(uint16_t offset, const uint8_t* data, uint16_t size)
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

HAL_StatusTypeDef Eeprom24AA01::writePage(uint16_t offset, const uint8_t* data, uint16_t size)
{
    const HAL_StatusTypeDef status = bus_.memWrite(address_, offset, data, size,
                                                   kTransferTimeoutMs);
    if (status != HAL_OK) {
        return fail(status);
    }
    return waitForWriteCycle();
}

HAL_StatusTypeDef Eeprom24AA01::waitForWriteCycle()
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

HAL_StatusTypeDef Eeprom24AA01::readByte(uint16_t offset, uint8_t& value)
{
    return read(offset, &value, 1U);
}

HAL_StatusTypeDef Eeprom24AA01::writeByte(uint16_t offset, uint8_t value)
{
    return write(offset, &value, 1U);
}

} // namespace Device
