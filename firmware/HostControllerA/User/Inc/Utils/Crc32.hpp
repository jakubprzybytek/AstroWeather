#pragma once

#include <cstdint>

// The standard reflected CRC-32 (IEEE 802.3, zlib, PNG): polynomial 0x04C11DB7
// reflected to 0xEDB88320, initial value and final XOR 0xFFFFFFFF. Bitwise, so
// it needs no table in RAM or flash. Check value: "123456789" -> 0xCBF43926.
//
// For data that arrives in pieces, start from kInitial, update() each piece
// and finish() the result; compute() does all three for one buffer.
namespace Crc32 {

constexpr uint32_t kInitial = 0xFFFFFFFFU;
constexpr uint32_t kPolynomial = 0xEDB88320U;

inline uint32_t update(uint32_t crc, const uint8_t* data, uint32_t length)
{
    for (uint32_t index = 0U; index < length; ++index)
    {
        crc ^= data[index];
        for (uint32_t bit = 0U; bit < 8U; ++bit)
        {
            crc = (crc & 1U) != 0U
                ? (crc >> 1U) ^ kPolynomial
                : (crc >> 1U);
        }
    }
    return crc;
}

constexpr uint32_t finish(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFU;
}

inline uint32_t compute(const uint8_t* data, uint32_t length)
{
    return finish(update(kInitial, data, length));
}

} // namespace Crc32
