#include <Display/DisplayTypes.hpp>

#include <cmath>

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

// Digits drawn for a value: all of its significant digits, and at least the
// digit left of the decimal point, so 0.5 shows as "0.5" and not ".5".
uint8_t shownDigitCount(uint32_t magnitude, uint8_t precision)
{
    uint8_t count = 1U;
    while (magnitude >= 10U) {
        magnitude /= 10U;
        ++count;
    }
    return count > precision ? count : static_cast<uint8_t>(precision + 1U);
}

uint32_t magnitudeOf(int16_t mantissa)
{
    return mantissa < 0 ? static_cast<uint32_t>(-static_cast<int32_t>(mantissa))
                        : static_cast<uint32_t>(mantissa);
}

// The digits plus the minus sign of a negative value must fit the four
// digit positions.
bool fits(int16_t mantissa, uint8_t precision)
{
    if (precision > kMaxPrecision) {
        return false;
    }
    const uint32_t positions = shownDigitCount(magnitudeOf(mantissa), precision) +
                               (mantissa < 0 ? 1U : 0U);
    return positions <= 4U;
}

} // namespace

void NumericDisplay::setError()
{
    data_.slots.fill(kSegmentD);
    data_.slots[4] = 0U;
}

void NumericDisplay::setFixed(int16_t mantissa, uint8_t precision)
{
    if (!fits(mantissa, precision)) {
        setError();
        return;
    }
    data_.slots.fill(0U);
    uint32_t magnitude = magnitudeOf(mantissa);
    const uint8_t digits = shownDigitCount(magnitude, precision);
    for (uint8_t digit = 0U; digit < digits; ++digit) {
        data_.slots[static_cast<uint8_t>(3U - digit)] = kDigits[magnitude % 10U];
        magnitude /= 10U;
    }
    if (precision != 0U) {
        data_.slots[static_cast<uint8_t>(3U - precision)] |= kSegmentDp;
    }
    if (mantissa < 0) {
        data_.slots[static_cast<uint8_t>(3U - digits)] = kMinus;
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
    data_.slots[0] = hour < 10U ? 0U : kDigits[hour / 10U];
    data_.slots[1] = kDigits[hour % 10U];
    data_.slots[2] = kDigits[minute / 10U];
    data_.slots[3] = kDigits[minute % 10U];
    data_.slots[4] = kSegmentA | kSegmentB;
}

void NumericDisplay::setTimeUnset()
{
    data_.slots[0] = kMinus;
    data_.slots[1] = kMinus;
    data_.slots[2] = kMinus;
    data_.slots[3] = kMinus;
    data_.slots[4] = kSegmentA | kSegmentB;
}

void NumericDisplay::setBlank()
{
    data_.slots.fill(0U);
}

} // namespace Display
