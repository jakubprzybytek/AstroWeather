#include <FrameAssembler.hpp>

#include <Display/DisplayI2cProtocol.hpp>

#include <Expect.hpp>

// The staging rules: attributes wait for the next content, content applies
// them, they persist until replaced, and a rejected message changes nothing.

namespace {

using Test::expect;
using Test::expectEqual;
using DisplayController::FrameAssembler;

Display::LogicalBoardState plane(uint8_t seed)
{
    Display::LogicalBoardState state{};
    for (uint8_t display = 0; display < Display::kNumericDisplayCount; ++display) {
        for (uint8_t slot = 0; slot < Display::kSlotCount; ++slot) {
            state.numeric[display].slots[slot] = static_cast<uint8_t>(seed + display * 8U + slot);
        }
    }
    for (uint8_t row = 0; row < Display::kMatrixRowCount; ++row) {
        state.matrix[row] = (0x010101UL * seed + row) & Display::kMatrixMask;
    }
    return state;
}

Display::I2cMessage message(uint8_t command, const Display::LogicalBoardState& state)
{
    Display::I2cMessage out{};
    Display::serializePlaneI2c(command, state, out);
    return out;
}

FrameAssembler::Result send(FrameAssembler& assembler, uint8_t command,
                            const Display::LogicalBoardState& state)
{
    const Display::I2cMessage out = message(command, state);
    return assembler.accept(out.data(), out.size());
}

void testContentAloneUsesDefaults()
{
    FrameAssembler assembler;
    expectEqual(send(assembler, Display::kSetDisplayCommand, plane(1)), FrameAssembler::Result::Content,
                "content accepted");
    expect(assembler.content() == plane(1), "content stored");
    expect(assembler.attributes().blink == Display::LogicalBoardState{}, "no blink by default");
    expect(assembler.attributes().level0 == Display::allElements(), "full level by default");
    expect(assembler.attributes().level1 == Display::allElements(), "full level by default (bit 1)");
}

void testAttributesWaitForContent()
{
    FrameAssembler assembler;
    send(assembler, Display::kSetDisplayCommand, plane(1));

    expectEqual(send(assembler, Display::kSetBlinkCommand, plane(2)), FrameAssembler::Result::Staged,
                "blink staged");
    expectEqual(send(assembler, Display::kSetLevel0Command, plane(3)), FrameAssembler::Result::Staged,
                "level 0 staged");
    expectEqual(send(assembler, Display::kSetLevel1Command, plane(4)), FrameAssembler::Result::Staged,
                "level 1 staged");
    expect(assembler.attributes().blink == Display::LogicalBoardState{},
           "staged blink not applied before content");
    expect(assembler.content() == plane(1), "content unchanged by staging");

    expectEqual(send(assembler, Display::kSetDisplayCommand, plane(5)), FrameAssembler::Result::Content,
                "content applies the staged attributes");
    expect(assembler.content() == plane(5), "new content");
    expect(assembler.attributes().blink == plane(2), "blink applied");
    expect(assembler.attributes().level0 == plane(3), "level 0 applied");
    expect(assembler.attributes().level1 == plane(4), "level 1 applied");
}

void testAttributesPersistUntilReplaced()
{
    FrameAssembler assembler;
    send(assembler, Display::kSetBlinkCommand, plane(2));
    send(assembler, Display::kSetDisplayCommand, plane(1));
    send(assembler, Display::kSetDisplayCommand, plane(6));
    expect(assembler.attributes().blink == plane(2), "content alone keeps the last blink");
    expect(assembler.content() == plane(6), "content updated");

    send(assembler, Display::kSetBlinkCommand, plane(7));
    expect(assembler.attributes().blink == plane(2), "new blink waits for content");
    send(assembler, Display::kSetDisplayCommand, plane(8));
    expect(assembler.attributes().blink == plane(7), "then replaces the old one");
}

void testRejectedMessagesChangeNothing()
{
    FrameAssembler assembler;
    send(assembler, Display::kSetBlinkCommand, plane(2));
    send(assembler, Display::kSetDisplayCommand, plane(1));

    Display::I2cMessage bad = message(0x05U, plane(9));
    expectEqual(assembler.accept(bad.data(), bad.size()), FrameAssembler::Result::Rejected,
                "unknown command rejected");
    bad = message(Display::kSetDisplayCommand, plane(9));
    expectEqual(assembler.accept(bad.data(), bad.size() - 1U), FrameAssembler::Result::Rejected,
                "short message rejected");
    expectEqual(assembler.accept(nullptr, bad.size()), FrameAssembler::Result::Rejected,
                "null rejected");
    expect(assembler.content() == plane(1), "content kept");
    expect(assembler.attributes().blink == plane(2), "attributes kept");

    // A rejected attribute does not disturb what is staged either.
    send(assembler, Display::kSetLevel0Command, plane(3));
    bad = message(Display::kSetLevel1Command, plane(4));
    assembler.accept(bad.data(), 10U);
    send(assembler, Display::kSetDisplayCommand, plane(1));
    expect(assembler.attributes().level0 == plane(3), "staged level 0 applied");
    expect(assembler.attributes().level1 == Display::allElements(), "rejected level 1 not staged");
}

} // namespace

int main()
{
    testContentAloneUsesDefaults();
    testAttributesWaitForContent();
    testAttributesPersistUntilReplaced();
    testRejectedMessagesChangeNothing();
    return Test::finish("FrameAssembler");
}
