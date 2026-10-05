#pragma once

#include <Display/DisplayTypes.hpp>

#include <cstdint>

// Whole-board test screens the DisplayController shows on its switches. The
// boot screens, shared with the host, are in Display/BootScreens.hpp. Pure
// functions, tested natively.
namespace DisplayController {

// Every segment, indicator and matrix dot on: the boot self-test.
Display::LogicalBoardState allSegmentsState();

// Numeric display n (0-3) shows "nnnn" with n counted from 1; matrix row r
// (0 = top) lights its first r + 1 columns. Checks which display and row is
// which, and the matrix orientation.
Display::LogicalBoardState identifyState();

} // namespace DisplayController
