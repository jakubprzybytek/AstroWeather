#pragma once

#include <Display/PcbDisplayBoard.hpp>
#include <Display/TimelineSync.hpp>
#include <I2cTarget.hpp>

#include <cstdint>

// Keeps this board's refresh timeline on the host's: takes each sync the
// host broadcasts, decides with Display::TimelineSync, and applies the
// decision to the refresh (frame numbers, phase, rate) and to HSITRIM. Tells
// the host, through the status byte it reads, whether more syncs are wanted.
// Runs on the DisplayApp task.
//
// HSITRIM moves at most kHsiLimit steps from where the board booted, and
// never across an HSICAL band edge (Device/HsiTrim.hpp); the servo covers
// the rest, up to 2 %.
class TimelineFollower {
public:
    static constexpr uint8_t kHsiLimit = 8U;

    TimelineFollower(Display::PcbDisplayBoard& board, I2cTarget& link);

    // Before the first sync: notes the boot trim and asks for syncs.
    void init();
    void onSync(const I2cTarget::Sync& sync);

private:
    Display::PcbDisplayBoard& board_;
    I2cTarget& link_;
    Display::TimelineSync sync_;
    uint8_t bootTrim_ = 64U;
};
