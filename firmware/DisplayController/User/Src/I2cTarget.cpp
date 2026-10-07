#include <I2cTarget.hpp>

#include <Debug/PulseLed.hpp>
#include <Display/PcbDisplayBoard.hpp>
#include <Stats.hpp>
#include <Utils/MicroClock.hpp>

#include "FreeRTOS.h"
#include "task.h"

namespace {

I2cTarget* instance = nullptr;

} // namespace

I2cTarget::I2cTarget(I2C_HandleTypeDef& handle) : handle_(handle)
{
    instance = this;
}

void I2cTarget::setRecipient(osThreadId_t recipient, uint32_t flag, uint32_t syncFlag)
{
    recipient_ = recipient;
    flag_ = flag;
    syncFlag_ = syncFlag;
}

bool I2cTarget::begin(uint16_t address)
{
    address_ = address;
    // HAL_I2C_Init() on an initialised handle only rewrites the registers:
    // it disables the peripheral, sets OAR1 and re-enables it. It rewrites
    // CR1 too, which leaves the analog filter on (its default) and the
    // digital filter off, as CubeMX configures them.
    handle_.Init.OwnAddress1 = static_cast<uint32_t>(address) << 1U;
    // The host's timeline sync comes to the general-call address. Set here
    // rather than in CubeMX, which keeps it disabled.
    handle_.Init.GeneralCallMode = I2C_GENERALCALL_ENABLE;
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
    const bool ready = count_ != 0U;
    if (ready) {
        message = pending_[head_];
        head_ = static_cast<uint8_t>((head_ + 1U) % kQueueDepth);
        count_ = static_cast<uint8_t>(count_ - 1U);
    }
    taskEXIT_CRITICAL();
    return ready;
}

bool I2cTarget::takeSync(Sync& sync)
{
    taskENTER_CRITICAL();
    const bool ready = syncReady_;
    if (ready) {
        sync = sync_;
        syncReady_ = false;
    }
    taskEXIT_CRITICAL();
    return ready;
}

void I2cTarget::onAddress(uint8_t direction, uint16_t matchCode)
{
    finishReceive();
    if (matchCode == Display::kGeneralCallAddress) {
        // The sync broadcast; a general-call read is not a thing. Stamp now:
        // the host's interrupts can stretch the data bytes that follow.
        receivingSync_ = true;
        syncStamped_ = Display::PcbDisplayBoard::stampNow(syncStamp_);
        HAL_I2C_Slave_Seq_Receive_IT(&handle_, syncBuffer_.data(),
                                     static_cast<uint16_t>(syncBuffer_.size()),
                                     I2C_FIRST_AND_LAST_FRAME);
        return;
    }
    if (direction == I2C_DIRECTION_TRANSMIT) {
        activityLed().pulse(kActivityPulseMs);
        // The host writes: receive one whole message.
        receiving_ = true;
        HAL_I2C_Slave_Seq_Receive_IT(&handle_, receiveBuffer_.data(),
                                     static_cast<uint16_t>(receiveBuffer_.size()),
                                     I2C_FIRST_AND_LAST_FRAME);
    } else {
        readReply_ = Display::boardStatus(syncLocked_, needsContent_);
        HAL_I2C_Slave_Seq_Transmit_IT(&handle_, &readReply_, 1U, I2C_FIRST_AND_LAST_FRAME);
    }
}

void I2cTarget::onReceiveComplete()
{
    if (receivingSync_) {
        receivingSync_ = false;
        if (syncStamped_) {
            const uint32_t dataMicros = Utils::microsNow() - syncStamp_.now;
            g_displayStats.syncDataMicros = dataMicros;
            if (dataMicros > g_displayStats.syncDataMaxMicros) {
                g_displayStats.syncDataMaxMicros = dataMicros;
            }
            sync_.message = syncBuffer_;
            sync_.stamp = syncStamp_;
            syncReady_ = true;
            ++g_displayStats.syncsReceived;
            if (recipient_ != nullptr) {
                osThreadFlagsSet(recipient_, syncFlag_);
            }
        }
        return;
    }
    receiving_ = false;
    // Interrupt context: the task's takeMessage() runs under a critical
    // section, so head_ and count_ are consistent here.
    if (count_ == kQueueDepth) {
        // Full: drop the oldest, so the newest is never the one lost.
        head_ = static_cast<uint8_t>((head_ + 1U) % kQueueDepth);
        count_ = static_cast<uint8_t>(count_ - 1U);
        ++g_displayStats.queueOverruns;
    }
    pending_[(head_ + count_) % kQueueDepth] = receiveBuffer_;
    count_ = static_cast<uint8_t>(count_ + 1U);
    if (recipient_ != nullptr) {
        osThreadFlagsSet(recipient_, flag_);
    }
}

void I2cTarget::onListenComplete()
{
    if (__HAL_I2C_GET_FLAG(&handle_, I2C_FLAG_ADDR) != RESET) {
        ++g_displayStats.stopWithAddrPending;
    }
    finishReceive();
    rearm();
}

void I2cTarget::onError()
{
    // A NACK ends every read by the host and every write shorter than a
    // message, including the host's address-only probe; only other errors
    // are counted.
    const uint32_t error = HAL_I2C_GetError(&handle_);
    g_displayStats.lastI2cError = error;
    if ((error & ~HAL_I2C_ERROR_AF) != 0U) {
        ++g_displayStats.i2cErrors;
    }
    finishReceive();
    rearm();
}

// A write that ended before a whole message: count it by how far the HAL's
// buffer pointer got (the error path clears XferCount but not pBuffPtr).
void I2cTarget::finishReceive()
{
    if (receivingSync_) {
        receivingSync_ = false;
        ++g_displayStats.syncsShort;
    }
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
    if (instance != nullptr && hi2c == &instance->handle()) {
        instance->onAddress(TransferDirection, AddrMatchCode);
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
