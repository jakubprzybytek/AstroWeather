#include <Astro/AstroDisplayMapper.hpp>

namespace AstroDisplayMapper {

Display::NumericSegments unavailableSegments()
{
    Display::NumericSegments unavailable{};
    unavailable.slots.fill(Display::kSegmentDp);
    unavailable.slots[4] = 0U;
    return unavailable;
}

void mapMatrixRow(const HostController::AstroMatrixRow& row, uint8_t index,
                  Display::MatrixRow target, Display::BoardAttributes& attributes)
{
    const uint32_t lit = row.lit & Display::kMatrixMask;
    const uint32_t unlit = ~lit & Display::kMatrixMask;
    target.setRow(lit);
    if (index < Display::kMatrixRowCount)
    {
        attributes.level0.matrix[index] = (row.level0 & lit) | unlit;
        attributes.level1.matrix[index] = (row.level1 & lit) | unlit;
    }
    attributes.setMatrixBlink(index, row.blink & lit);
}

void mapNumeric(Display::NumericDisplay display, const HostController::AstroNumericValue& value)
{
    if (!value.available)
    {
        display.setSegments(unavailableSegments());
    }
    else if (value.time)
    {
        display.setTime(value.hour, value.minute);
    }
    else
    {
        display.setValue(value.value, 0U);
    }
}

} // namespace AstroDisplayMapper
