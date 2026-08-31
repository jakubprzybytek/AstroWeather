#include <Display/DisplayTypes.hpp>

#include <cmath>
#include <limits>

namespace Display {
namespace {

constexpr uint8_t kSegmentA = 1U << 0U;
constexpr uint8_t kSegmentB = 1U << 1U;
constexpr uint8_t kSegmentD = 1U << 3U;
constexpr uint8_t kSegmentG = 1U << 6U;

constexpr uint8_t kDigits[] = {
    0x3FU, 0x06U, 0x5BU, 0x4FU, 0x66U,
    0x6DU, 0x7DU, 0x07U, 0x7FU, 0x6FU,
};
constexpr uint8_t kMinus = kSegmentG;

bool fits(int16_t mantissa, uint8_t precision)
{
    if (precision > kMaxPrecision || mantissa == std::numeric_limits<int16_t>::min()) {
        return false;
    }
    const int32_t magnitude = mantissa < 0 ? -static_cast<int32_t>(mantissa) : mantissa;
    return mantissa < 0 ? magnitude <= 999 : magnitude <= 9999;
}

} // namespace

void NumericDisplay::setError()
{
    data_.slots.fill(kSegmentD);
}

void NumericDisplay::setFixed(int16_t mantissa, uint8_t precision)
{
    if (!fits(mantissa, precision)) {
        setError();
        return;
    }
    data_.slots.fill(0U);
    int32_t magnitude = mantissa;
    const bool negative = magnitude < 0;
    if (negative) {
        magnitude = -magnitude;
        data_.slots[0] = kMinus;
    }
    const uint8_t digitCount = negative ? 3U : 4U;
    for (uint8_t position = negative ? 1U : 0U; position < 4U; ++position) {
        const uint8_t digit = static_cast<uint8_t>(digitCount - 1U - position);
        const uint32_t divisor = digit == 0U ? 1U :
            digit == 1U ? 10U : digit == 2U ? 100U : 1000U;
        const uint8_t number = static_cast<uint8_t>((magnitude / divisor) % 10);
        const bool leading = number == 0U && magnitude < static_cast<int32_t>(divisor * 10U);
        data_.slots[position] = leading ? 0U : kDigits[number];
        if (precision != 0U && position == static_cast<uint8_t>(3U - precision)) {
            data_.slots[position] |= 0x80U;
        }
    }
}

void NumericDisplay::setValue(int16_t value)
{
    setFixed(value, 0);
}

void NumericDisplay::setValue(float value, uint8_t precision)
{
    if (!std::isfinite(value) || precision > kMaxPrecision) {
        setError();
        return;
    }
    const float scale = precision == 0 ? 1.0F :
                        precision == 1 ? 10.0F :
                        precision == 2 ? 100.0F : 1000.0F;
    const float scaled = value * scale;
    if (!std::isfinite(scaled) || scaled > 32767.0F || scaled < -32768.0F) {
        setError();
        return;
    }
    const long rounded = std::lround(scaled);
    if (rounded < -32768L || rounded > 32767L) {
        setError();
        return;
    }
    setFixed(static_cast<int16_t>(rounded), precision);
}

void NumericDisplay::setTime(uint8_t hour, uint8_t minute)
{
    if (hour > 99U || minute > 99U) {
        setError();
        return;
    }
    data_.slots[0] = kDigits[hour / 10U];
    data_.slots[1] = kDigits[hour % 10U];
    data_.slots[2] = kDigits[minute / 10U];
    data_.slots[3] = kDigits[minute % 10U];
    data_.slots[4] = kSegmentA | kSegmentB;
}

void NumericDisplay::setBlank()
{
    data_.slots.fill(0U);
}

} // namespace Display
