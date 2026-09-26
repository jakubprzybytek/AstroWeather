#include <FrameAssembler.hpp>

#include <Display/DisplayI2cProtocol.hpp>

namespace DisplayController {

FrameAssembler::Result FrameAssembler::accept(const uint8_t* data, std::size_t size)
{
    uint8_t command = 0U;
    Display::LogicalBoardState plane{};
    if (!Display::deserializePlaneI2c(data, size, command, plane)) {
        return Result::Rejected;
    }
    if (Display::LogicalBoardState* target = Display::attributePlane(staged_, command)) {
        *target = plane;
        return Result::Staged;
    }
    content_ = plane;
    applied_ = staged_;
    return Result::Content;
}

} // namespace DisplayController
