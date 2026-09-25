#include <I2cTarget.hpp>

#include <Stats.hpp>

#include "FreeRTOS.h"
#include "task.h"

namespace {

I2cTarget* instance = nullptr;

} // namespace

I2cTarget::I2cTarget(I2C_HandleTypeDef& handle) : handle_(handle)
{
    instance = this;
}

void I2cTarget::setRecipient(osThreadId_t recipient, uint32_t flag)
{
    recipient_ = recipient;
    flag_ = flag;
}

bool I2cTarget::begin(uint16_t address)
{
    address_ = address;
    // HAL_I2C_Init() on an initialised handle only rewrites the registers:
    // it disables the peripheral, sets OAR1 and re-enables it, keeping the
    // analog filter CubeMX configured.
    handle_.Init.OwnAddress1 = static_cast<uint32_t>(address) << 1U;
    if (HAL_I2C_Init(&handle_) != HAL_OK) {
        return false;
    }
    return HAL_I2C_EnableListen_IT(&handle_) == HAL_OK;
}

void I2cTarget::ensureListening()
{
    // Never listen before begin(): that would answer on the placeholder address.
    if (address_ == 0U) {
        return;
    }
    if (HAL_I2C_GetState(&handle_) == HAL_I2C_STATE_READY) {
        if (HAL_I2C_EnableListen_IT(&handle_) == HAL_OK) {
            ++g_displayStats.listenRearms;
        }
    }
}

bool I2cTarget::takeMessage(Display::I2cMessage& message)
{
    taskENTER_CRITICAL();
    const bool ready = pendingReady_;
    if (ready) {
        message = pending_;
        pendingReady_ = false;
    }
    taskEXIT_CRITICAL();
    return ready;
}

void I2cTarget::onAddress(uint8_t direction)
{
    finishReceive();
    if (direction == I2C_DIRECTION_TRANSMIT) {
        // The host writes: receive one whole message.
        receiving_ = true;
        HAL_I2C_Slave_Seq_Receive_IT(&handle_, receiveBuffer_.data(),
                                     static_cast<uint16_t>(receiveBuffer_.size()),
                                     I2C_FIRST_AND_LAST_FRAME);
    } else {
        HAL_I2C_Slave_Seq_Transmit_IT(&handle_, &readReply_, 1U, I2C_FIRST_AND_LAST_FRAME);
    }
}

void I2cTarget::onReceiveComplete()
{
    receiving_ = false;
    pending_ = receiveBuffer_;
    pendingReady_ = true;
    if (recipient_ != nullptr) {
        osThreadFlagsSet(recipient_, flag_);
    }
}

void I2cTarget::onListenComplete()
{
    finishReceive();
    rearm();
}

void I2cTarget::onError()
{
    // A NACK ends every read by the host and every write shorter than a
    // message, including the host's address-only probe; only other errors
    // are counted.
    if ((HAL_I2C_GetError(&handle_) & ~HAL_I2C_ERROR_AF) != 0U) {
        ++g_displayStats.i2cErrors;
    }
    finishReceive();
    rearm();
}

// A write that ended before a whole message: count it by how far the HAL's
// buffer pointer got (the error path clears XferCount but not pBuffPtr).
void I2cTarget::finishReceive()
{
    if (!receiving_) {
        return;
    }
    receiving_ = false;
    if (handle_.pBuffPtr == receiveBuffer_.data()) {
        ++g_displayStats.probes;
    } else {
        ++g_displayStats.shortWrites;
    }
}

void I2cTarget::rearm()
{
    if (address_ != 0U && HAL_I2C_GetState(&handle_) == HAL_I2C_STATE_READY) {
        HAL_I2C_EnableListen_IT(&handle_);
    }
}

extern "C" void HAL_I2C_AddrCallback(I2C_HandleTypeDef* hi2c, uint8_t TransferDirection,
                                     uint16_t AddrMatchCode)
{
    (void)AddrMatchCode;
    if (instance != nullptr && hi2c == &instance->handle()) {
        instance->onAddress(TransferDirection);
    }
}

extern "C" void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef* hi2c)
{
    if (instance != nullptr && hi2c == &instance->handle()) {
        instance->onReceiveComplete();
    }
}

extern "C" void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef* hi2c)
{
    if (instance != nullptr && hi2c == &instance->handle()) {
        instance->onListenComplete();
    }
}

extern "C" void HAL_I2C_ErrorCallback(I2C_HandleTypeDef* hi2c)
{
    if (instance != nullptr && hi2c == &instance->handle()) {
        instance->onError();
    }
}
