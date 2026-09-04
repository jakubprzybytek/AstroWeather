#pragma once

#include <cstdint>

namespace CurrentSense {

constexpr uint32_t rawToMilliAmps(uint32_t raw,
                                  uint32_t referenceMilliVolts = 3300U)
{
    constexpr uint32_t kAdcMaxValue = 4095U;
    constexpr uint32_t kSenseScaleMilliVoltsPerAmp = 2500U;

    return static_cast<uint32_t>(
        (static_cast<uint64_t>(raw) * referenceMilliVolts * 1000ULL) /
        (static_cast<uint64_t>(kAdcMaxValue) *
         kSenseScaleMilliVoltsPerAmp));
}

}  // namespace CurrentSense
