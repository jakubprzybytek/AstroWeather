#pragma once

#include <Display/DisplayBoard.hpp>
#include <Display/DisplayTypes.hpp>

#include <cstdint>

// What every board, the host's own included, shows right after power-up: the
// slot test, then its I2C address. The states are pure and tested natively;
// showBootScreens() runs the sequence with osDelay.
namespace Display {

// Each slot (one DISPLAYx_EN switch) is lit on its own for this long, so the
// test takes kSlotCount * kSlotTestMs = 1 s.
constexpr uint32_t kSlotTestMs = 200U;
constexpr uint32_t kBootAddressMs = 2000U;

// Digit `slot` (0-4) of every numeric display with its decimal point (slot 4:
// the indicators L1-L3) and matrix row `slot`, so the matrix steps from the
// top row down. That row is driven by slot 4 - slot (see DisplayCodec.cpp),
// not by `slot`; only the middle step matches. A dead slot switch shows as a
// step with its digits unlit.
LogicalBoardState slotTestState(uint8_t slot);

// "Ad" and the 7-bit I2C address in hex on numeric display 1, e.g. "Ad12" for
// 0x12; "Ad--" for an address outside 0x01-0xFF. Everything else blank.
LogicalBoardState addressState(uint16_t address);

// The slot test, then the address, each frame shown with board.show() at full
// brightness and no blinking, so the board's own state is left alone.
// Blocks for about 3 s.
void showBootScreens(DisplayBoard& board, uint16_t address);

} // namespace Display
