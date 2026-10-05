#pragma once

#include <cstdint>

// Counters for bring-up and diagnosis, until the board has a console. The
// struct has C linkage, so it can be found and read over SWD without
// demangling:
//
//   arm-none-eabi-nm build/Debug/DisplayController.elf | grep g_displayStats
//   STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -r32 <address> 0x44
//
// Each field has a single writer (the I2C interrupt or the DisplayApp task), so
// no locking is needed.
extern "C" {

struct DisplayControllerStats {
    uint32_t address;             // 7-bit I2C address from the straps, 0 if none
    uint32_t framesAccepted;      // content messages shown (command 0x01)
    uint32_t framesRejected;      // 36 bytes received but not a known command
    uint32_t shortWrites;         // writes that ended before 36 bytes
    uint32_t probes;              // address-only writes, e.g. the host's 'status'
    uint32_t i2cErrors;           // bus errors other than a NACK
    uint32_t listenRearms;        // times listening had to be restarted
    uint32_t staleTimeouts;       // times the data went stale and "no data" showed
    uint32_t lastFrameTick;       // kernel tick (ms) of the last accepted frame
    uint32_t attributesAccepted;  // attribute messages staged (commands 0x02-0x04)
    uint32_t queueOverruns;       // messages dropped because the queue was full
    uint32_t refreshFrames;       // display frames shown, copied once a second
    uint32_t lateShifts;          // refresh interrupts later than a whole pass
    uint32_t lateInterrupts;      // refresh interrupts later than the pass they start
    uint32_t maxInterruptMicros;  // longest refresh interrupt
    uint32_t lastI2cError;        // HAL error bits at the last error callback, NACK included
    // A transfer's STOP handled after the host's next address had matched:
    // the I2C interrupt ran late, the race behind the 2026-10-05 NACKs (see
    // I2cTarget.hpp). Should stay 0.
    uint32_t stopWithAddrPending;
};

extern volatile DisplayControllerStats g_displayStats;

} // extern "C"
