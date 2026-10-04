#pragma once

#include <Display/DisplayTypes.hpp>

#include "cmsis_os2.h"
#include "main.h"

#include <array>
#include <cstdint>

// The board's side of the I2C link to the host: listens on its own address,
// receives the 36-byte display message in interrupts, and hands it to a task
// through a thread flag. The message format is Display::deserializeI2c();
// see HostControllerA/docs/Display.md#i2c-transport.
//
// One instance, for hi2c1; the HAL callbacks are routed to it. Every
// transaction addressed to this board, read or write, pulses LED_2.
class I2cTarget {
public:
    // As long as the host's pulse for one USB CDC transfer.
    static constexpr uint32_t kActivityPulseMs = 20U;

    explicit I2cTarget(I2C_HandleTypeDef& handle);

    // Thread and flag to signal when a complete message has arrived. Set
    // before begin().
    void setRecipient(osThreadId_t recipient, uint32_t flag);

    // Re-initialises the peripheral with this 7-bit own address and starts
    // listening. Until this runs, CubeMX's placeholder address 0x10 is set
    // but nothing services it.
    bool begin(uint16_t address);

    // Restarts listening if the peripheral has dropped out of it, for example
    // after a bus error. Cheap; the owning task calls it periodically.
    void ensureListening();

    // Copies the oldest complete message not yet taken; false if none. The
    // host sends a board's attributes and content as four messages a few
    // milliseconds apart, so several can be waiting; when the queue is full
    // the oldest is dropped and counted.
    static constexpr uint8_t kQueueDepth = 4U;
    bool takeMessage(Display::I2cMessage& message);

    uint16_t address() const { return address_; }
    I2C_HandleTypeDef& handle() { return handle_; }

    // Interrupt context, from the HAL callbacks.
    void onAddress(uint8_t direction);
    void onReceiveComplete();
    void onListenComplete();
    void onError();

private:
    void finishReceive();
    void rearm();

    I2C_HandleTypeDef& handle_;
    uint16_t address_ = 0U;
    osThreadId_t recipient_ = nullptr;
    uint32_t flag_ = 0U;

    Display::I2cMessage receiveBuffer_{};
    // Ring of complete messages: head_ is the oldest, count_ how many.
    std::array<Display::I2cMessage, kQueueDepth> pending_{};
    volatile uint8_t head_ = 0U;
    volatile uint8_t count_ = 0U;
    volatile bool receiving_ = false;
    // Answer to a read from the host, which the protocol does not use; kept
    // so a read does not hang the bus.
    uint8_t readReply_ = 0U;
};
