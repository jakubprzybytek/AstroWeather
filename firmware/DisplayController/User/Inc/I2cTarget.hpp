#pragma once

#include <Display/DisplayI2cProtocol.hpp>
#include <Display/DisplayTypes.hpp>
#include <Display/Timeline.hpp>

#include "cmsis_os2.h"
#include "main.h"

#include <array>
#include <cstdint>

// The board's side of the I2C link to the host: listens on its own address,
// receives the 36-byte display message in interrupts, and hands it to a task
// through a thread flag. The message format is Display::deserializeI2c();
// see firmware/Docs/I2C.md.
//
// It also answers the general-call address 0x00, where the host broadcasts
// its timeline sync (Display::kSyncCommand): the address-match interrupt
// stamps the board's own timeline at once, and the task gets the message and
// the stamp through a second thread flag. A one-byte read answers the status
// the tasks set: whether the timeline is locked and whether content is wanted
// (Display::boardStatus()).
//
// One instance, for hi2c1; the HAL callbacks are routed to it. Every write
// addressed to this board pulses LED_2; the sync broadcast and the host's
// status reads do not, so LED_2 still shows the data traffic.
//
// I2C1's interrupt has priority 1, above the refresh timer, DMA and EXTI
// (3). The HAL sets CR2.NACK when it handles a transfer's STOP, and software
// cannot clear that bit: only an address match, a STOP or a sent NACK does.
// Handled late - after the host's next address had already matched, because
// a ~170 us refresh interrupt held it off - the NACK stayed set and the board
// refused the next message's second byte; the host saw error 0x4 about one
// refresh in seven (2026-10-05, traced on the board).
class I2cTarget {
public:
    // As long as the host's pulse for one USB CDC transfer.
    static constexpr uint32_t kActivityPulseMs = 20U;

    explicit I2cTarget(I2C_HandleTypeDef& handle);

    // Thread and flags to signal when a complete message, or a sync, has
    // arrived. Set before begin().
    void setRecipient(osThreadId_t recipient, uint32_t flag, uint32_t syncFlag);

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

    // The latest sync from the host, with the board's timeline when it
    // arrived; false if none since the last call. A newer sync replaces one
    // not yet taken.
    struct Sync {
        Display::SyncMessage message;
        Display::TimelineStamp stamp;
    };
    bool takeSync(Sync& sync);

    // The answer to the host's one-byte read (Display::boardStatus()): the
    // timeline locked, and content wanted.
    void setSyncLocked(bool locked) { syncLocked_ = locked; }
    void setNeedsContent(bool needs) { needsContent_ = needs; }

    uint16_t address() const { return address_; }
    I2C_HandleTypeDef& handle() { return handle_; }

    // Interrupt context, from the HAL callbacks.
    void onAddress(uint8_t direction, uint16_t matchCode);
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
    uint32_t syncFlag_ = 0U;

    Display::I2cMessage receiveBuffer_{};
    // Ring of complete messages: head_ is the oldest, count_ how many.
    std::array<Display::I2cMessage, kQueueDepth> pending_{};
    volatile uint8_t head_ = 0U;
    volatile uint8_t count_ = 0U;
    volatile bool receiving_ = false;

    Display::SyncMessage syncBuffer_{};
    Display::TimelineStamp syncStamp_{};
    Sync sync_{};
    volatile bool syncReady_ = false;
    volatile bool receivingSync_ = false;
    volatile bool syncStamped_ = false;

    // Answer to a read from the host; set by the task, combined into
    // readReply_ as the read starts. From boot: not locked, content wanted.
    volatile bool syncLocked_ = false;
    volatile bool needsContent_ = true;
    uint8_t readReply_ = 0U;
};
