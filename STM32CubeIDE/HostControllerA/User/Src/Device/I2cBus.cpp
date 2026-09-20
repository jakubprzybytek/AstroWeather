#include <Device/I2cBus.hpp>

namespace Device {

HAL_StatusTypeDef I2cBus::transmit(uint16_t deviceAddress, const uint8_t* data, uint16_t size,
                                   uint32_t timeoutMs)
{
    MutexGuard guard(mutex_);
    return HAL_I2C_Master_Transmit(&handle_, static_cast<uint16_t>(deviceAddress << 1U),
                                   const_cast<uint8_t*>(data), size, timeoutMs);
}

HAL_StatusTypeDef I2cBus::memRead(uint16_t deviceAddress, uint16_t memAddress, uint8_t* data,
                                  uint16_t size, uint32_t timeoutMs)
{
    MutexGuard guard(mutex_);
    return HAL_I2C_Mem_Read(&handle_, static_cast<uint16_t>(deviceAddress << 1U), memAddress,
                            I2C_MEMADD_SIZE_8BIT, data, size, timeoutMs);
}

HAL_StatusTypeDef I2cBus::memWrite(uint16_t deviceAddress, uint16_t memAddress, const uint8_t* data,
                                   uint16_t size, uint32_t timeoutMs)
{
    MutexGuard guard(mutex_);
    return HAL_I2C_Mem_Write(&handle_, static_cast<uint16_t>(deviceAddress << 1U), memAddress,
                             I2C_MEMADD_SIZE_8BIT, const_cast<uint8_t*>(data), size, timeoutMs);
}

HAL_StatusTypeDef I2cBus::isDeviceReady(uint16_t deviceAddress, uint32_t trials, uint32_t timeoutMs)
{
    MutexGuard guard(mutex_);
    return HAL_I2C_IsDeviceReady(&handle_, static_cast<uint16_t>(deviceAddress << 1U), trials,
                                 timeoutMs);
}

} // namespace Device
