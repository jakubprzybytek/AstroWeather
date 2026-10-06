#pragma once

#include <cstdint>

// The HSI16 oscillator's user trim, RCC_ICSCR.HSITRIM: 0-127, 64 by default,
// higher runs faster, about 0.33 % per step (measured on a G070 on
// 2026-10-05). Both boards clock everything from HSI16, so a trim moves the
// whole chip: the refresh, the I2C timing, the FreeRTOS tick. It applies at
// once and is lost at reset.
//
// ICSCR.HSICAL reads back the factory calibration with the trim added in.
// AN5126 warns the frequency can step backwards where that sum crosses a
// multiple of 64, so allowedSteps() never crosses one.
namespace HsiTrim {

constexpr uint8_t kDefault = 64U;
constexpr uint8_t kMax = 127U;

uint8_t trim();
// HSICAL as read back: the factory value plus the trim's offset from 64.
uint8_t calibration();
// Any value 0-127; false, and nothing changed, beyond that.
bool set(uint8_t trim);

// How much of `steps` the trim may move from `trim`: staying within
// +-`limit` of `start` and 0-127, and without HSICAL crossing a multiple of
// 64. Pure; 0 if none of it.
inline int32_t allowedSteps(uint8_t calibration, uint8_t trim, int32_t steps, uint8_t start,
                            uint8_t limit)
{
    int32_t target = static_cast<int32_t>(trim) + steps;
    const int32_t low = static_cast<int32_t>(start) - limit;
    const int32_t high = static_cast<int32_t>(start) + limit;
    target = (target < low) ? low : ((target > high) ? high : target);
    target = (target < 0) ? 0 : ((target > kMax) ? kMax : target);
    int32_t allowed = target - static_cast<int32_t>(trim);
    const int32_t band = calibration / 64;
    while (allowed != 0 && (static_cast<int32_t>(calibration) + allowed) / 64 != band) {
        allowed += (allowed > 0) ? -1 : 1;
    }
    return allowed;
}

} // namespace HsiTrim
