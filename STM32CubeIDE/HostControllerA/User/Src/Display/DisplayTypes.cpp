#include <Display/DisplayTypes.hpp>

#include <cmath>
#include <limits>

namespace Display {
namespace {

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
    data_.mantissa = 0;
    data_.flags = static_cast<uint8_t>(NumericMode::Error);
}

void NumericDisplay::setFixed(int16_t mantissa, uint8_t precision)
{
    if (!fits(mantissa, precision)) {
        setError();
        return;
    }
    data_.mantissa = mantissa;
    data_.flags = static_cast<uint8_t>(NumericMode::Value) |
                  static_cast<uint8_t>(precision << 2U);
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
    data_.mantissa = static_cast<int16_t>(hour * 100U + minute);
    data_.flags = static_cast<uint8_t>(NumericMode::Time) | kDoubleDotsMask;
}

void NumericDisplay::setBlank()
{
    data_.mantissa = 0;
    data_.flags = static_cast<uint8_t>(NumericMode::Blank);
}

} // namespace Display
