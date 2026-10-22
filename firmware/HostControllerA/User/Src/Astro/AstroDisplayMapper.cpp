#include <Astro/AstroDisplayMapper.hpp>

namespace AstroDisplayMapper {

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
    (void)display.setText(value.text.data());
}

} // namespace AstroDisplayMapper
