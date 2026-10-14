#include <Device/I2cBus.hpp>

#include <Debug/ActivityLedBridge.h>

namespace Device {

HAL_StatusTypeDef I2cBus::transmit(uint16_t deviceAddress, const uint8_t* data, uint16_t size,
                                   uint32_t timeoutMs, uint32_t* errorCode)
{
    MutexGuard guard(mutex_);
    const HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(
        &handle_, static_cast<uint16_t>(deviceAddress << 1U), const_cast<uint8_t*>(data), size,
        timeoutMs);
    if (errorCode != nullptr) {
        *errorCode = HAL_I2C_GetError(&handle_);
    }
    afterTransfer(status);
    return status;
}

HAL_StatusTypeDef I2cBus::receive(uint16_t deviceAddress, uint8_t* data, uint16_t size,
                                  uint32_t timeoutMs)
{
    MutexGuard guard(mutex_);
    const HAL_StatusTypeDef status = HAL_I2C_Master_Receive(
        &handle_, static_cast<uint16_t>(deviceAddress << 1U), data, size, timeoutMs);
    afterTransfer(status);
    return status;
}

HAL_StatusTypeDef I2cBus::memRead(uint16_t deviceAddress, uint16_t memAddress, uint8_t* data,
                                  uint16_t size, uint32_t timeoutMs)
{
    MutexGuard guard(mutex_);
    const HAL_StatusTypeDef status =
        HAL_I2C_Mem_Read(&handle_, static_cast<uint16_t>(deviceAddress << 1U), memAddress,
                         I2C_MEMADD_SIZE_8BIT, data, size, timeoutMs);
    afterTransfer(status);
    return status;
}

HAL_StatusTypeDef I2cBus::memWrite(uint16_t deviceAddress, uint16_t memAddress, const uint8_t* data,
                                   uint16_t size, uint32_t timeoutMs)
{
    MutexGuard guard(mutex_);
    const HAL_StatusTypeDef status =
        HAL_I2C_Mem_Write(&handle_, static_cast<uint16_t>(deviceAddress << 1U), memAddress,
                          I2C_MEMADD_SIZE_8BIT, const_cast<uint8_t*>(data), size, timeoutMs);
    afterTransfer(status);
    return status;
}

HAL_StatusTypeDef I2cBus::isDeviceReady(uint16_t deviceAddress, uint32_t trials, uint32_t timeoutMs)
{
    MutexGuard guard(mutex_);
    const HAL_StatusTypeDef status = HAL_I2C_IsDeviceReady(
        &handle_, static_cast<uint16_t>(deviceAddress << 1U), trials, timeoutMs);
    afterTransfer(status);
    return status;
}

void I2cBus::afterTransfer(HAL_StatusTypeDef status)
{
    ActivityLed_Pulse(ACTIVITY_LED_COMMS_PULSE_MS);
    recoverAfterFailure(status);
}

void I2cBus::recoverAfterFailure(HAL_StatusTypeDef status)
{
    // A NACK ends with a STOP the HAL has already sent; anything else (a
    // timeout, a bus error, lost arbitration, the bus found busy) may leave the
    // peripheral mid-transfer. IsDeviceReady() with no answer reports a
    // timeout after a STOP; the reset then costs a few cycles and no harm. Clearing PE is the peripheral's software reset:
    // it releases SCL and SDA and clears the state machine and the flags
    // (RM0444, I2C software reset: PE low, read back as 0, then set again).
    if (status == HAL_OK || HAL_I2C_GetError(&handle_) == HAL_I2C_ERROR_AF) {
        return;
    }
    __HAL_I2C_DISABLE(&handle_);
    while ((handle_.Instance->CR1 & I2C_CR1_PE) != 0U) {
    }
    __HAL_I2C_ENABLE(&handle_);
}

} // namespace Device
