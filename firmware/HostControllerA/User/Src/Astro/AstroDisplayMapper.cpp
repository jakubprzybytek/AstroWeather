#include <Astro/AstroDisplayMapper.hpp>

namespace AstroDisplayMapper {

Display::NumericSegments unavailableSegments()
{
    Display::NumericSegments unavailable{};
    unavailable.slots.fill(Display::kSegmentDp);
    unavailable.slots[4] = 0U;
    return unavailable;
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
        display.setValue(value.value, 1U);
    }
}

} // namespace AstroDisplayMapper
