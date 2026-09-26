#pragma once

#include <Display/DisplayTypes.hpp>

#include <cstddef>
#include <cstdint>

namespace DisplayController {

// Puts the host's messages back together into what the board shows. The
// attribute messages (blink, level bits) are only staged; the content
// message takes effect together with whatever attributes are staged by then,
// so content and attributes always change as one. Staged attributes persist
// until the host replaces them: a host that sends only content keeps the
// last attributes, and one that never sends any gets the defaults. Pure,
// tested natively.
class FrameAssembler {
public:
    enum class Result : uint8_t {
        Rejected,  // not a known 36-byte message; nothing changes
        Staged,    // an attribute plane, kept for the next content
        Content,   // new content and attributes to show
    };

    Result accept(const uint8_t* data, std::size_t size);

    // As of the last Content result.
    const Display::LogicalBoardState& content() const { return content_; }
    const Display::BoardAttributes& attributes() const { return applied_; }

private:
    Display::BoardAttributes staged_{};
    Display::BoardAttributes applied_{};
    Display::LogicalBoardState content_{};
};

} // namespace DisplayController
