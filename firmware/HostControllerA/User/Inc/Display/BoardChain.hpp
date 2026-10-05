#pragma once

#include <cstdint>

// Which board shows which forecast block. Each board, the host included, reads
// its I2C address from its straps (Display::detectBoardAddress()); block n goes
// to the board at 0x10 + n. The host's own display is not special: it shows
// tonight only because a host with no straps fitted is 0x10. Hardware-free, so
// the mapping is tested natively.
namespace Display {

// One position per forecast block, 0x10 to 0x15.
constexpr uint8_t kChainLength = 6U;
constexpr uint16_t kChainFirstAddress = 0x10U;
// chainPosition() of an address with no block.
constexpr uint8_t kNotInChain = 0xFFU;

constexpr uint16_t chainAddress(uint8_t position)
{
    return static_cast<uint16_t>(kChainFirstAddress + position);
}

constexpr uint8_t chainPosition(uint16_t address)
{
    return (address >= kChainFirstAddress && address < kChainFirstAddress + kChainLength)
               ? static_cast<uint8_t>(address - kChainFirstAddress)
               : kNotInChain;
}

} // namespace Display
