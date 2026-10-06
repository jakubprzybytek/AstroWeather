#pragma once

#include <Utils/Mutex.hpp>

#include "main.h"
#include "FreeRTOS.h"
#include "task.h"

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

    // `errorCode`, if given, gets the HAL error bits (HAL_I2C_ERROR_*) of this
    // transfer, read under the lock: a failed transfer is HAL_ERROR whether
    // it was a NACK, a bus error or the timeout.
    HAL_StatusTypeDef transmit(uint16_t deviceAddress, const uint8_t* data, uint16_t size,
                               uint32_t timeoutMs, uint32_t* errorCode = nullptr);
    // For a message that carries the instant it goes out: once the bus is
    // free, calls fill(data) with interrupts off and then transmits with the
    // scheduler suspended, so no task runs between the fill and the START.
    // The scheduler is suspended inside the critical section, so no tick is
    // held back before the fill reads the time. About 1 ms for a few bytes.
    template <typename Fill>
    HAL_StatusTypeDef transmitFilled(uint16_t deviceAddress, uint8_t* data, uint16_t size,
                                     uint32_t timeoutMs, Fill fill)
    {
        MutexGuard guard(mutex_);
        taskENTER_CRITICAL();
        fill(data);
        vTaskSuspendAll();
        taskEXIT_CRITICAL();
        const HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(
            &handle_, static_cast<uint16_t>(deviceAddress << 1U), data, size, timeoutMs);
        (void)xTaskResumeAll();
        return status;
    }
    HAL_StatusTypeDef receive(uint16_t deviceAddress, uint8_t* data, uint16_t size,
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
