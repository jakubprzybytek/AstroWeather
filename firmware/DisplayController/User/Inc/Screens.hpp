#pragma once

#include <Display/DisplayTypes.hpp>

#include <cstdint>

// Whole-board contents the DisplayController shows on its own, besides the
// host's data and Display::noDataState(). Pure functions, tested natively.
namespace DisplayController {

// Every segment, indicator and matrix dot on: the boot self-test.
Display::LogicalBoardState allSegmentsState();

// Numeric display n (0-3) shows "nnnn" with n counted from 1; matrix row r
// (0 = top) lights its first r + 1 columns. Checks which display and row is
// which, and the matrix orientation.
Display::LogicalBoardState identifyState();

// "Ad" and the board's 7-bit I2C address in hex on every numeric display,
// e.g. "Ad12" for 0x12; "Ad--" for an address outside 0x01-0xFF. Matrix blank.
Display::LogicalBoardState addressState(uint16_t address);

} // namespace DisplayController
