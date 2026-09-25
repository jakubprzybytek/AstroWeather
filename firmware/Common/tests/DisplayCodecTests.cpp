#include <Display/DisplayCodec.hpp>

#include <Expect.hpp>

#include <string>

// Expected values come from docs/Display.md ("PCB Encoding", "Numeric Segment
// Wiring" and "Multiplexing Mapping"), not from the encoder's own tables.

namespace {

using Test::expectEqual;

// Numeric Segment Wiring, one row per segment A, B, C, D, E, F, G, DP; one
// column per numeric display 1 to 4 (software indices 0 to 3).
constexpr uint8_t kDocSegmentBit[8][4] = {
    {2, 5, 0, 0}, // A
    {1, 6, 1, 1}, // B
    {4, 1, 3, 7}, // C
    {3, 3, 6, 4}, // D
    {6, 2, 5, 5}, // E
    {0, 7, 2, 2}, // F
    {7, 4, 7, 3}, // G
    {5, 0, 4, 6}, // DP
};

// Wire order within a slot: numeric 4, numeric 3, matrix bytes 3, 2, 1,
// numeric 2, numeric 1.
constexpr uint8_t kNumericWireByte[4] = {6, 5, 1, 0};
constexpr uint8_t kMatrixByte1 = 4; // SCT bits 0-7, columns 1-8
constexpr uint8_t kMatrixByte2 = 3; // SCT bits 8-15, columns 9-16
constexpr uint8_t kMatrixByte3 = 2; // SCT bits 16-23, columns 17-21

constexpr uint8_t kA = 0x01U;
constexpr uint8_t kB = 0x02U;
constexpr uint8_t kC = 0x04U;
constexpr uint8_t kG = 0x40U;

std::string name(const char* what, unsigned a, unsigned b, unsigned c)
{
    return std::string(what) + " " + std::to_string(a) + "/" + std::to_string(b) + "/" +
           std::to_string(c);
}

uint8_t frameByte(const Display::PreparedFrame& frame, uint8_t slot, uint8_t byte)
{
    return frame[slot * Display::kBytesPerSlot + byte];
}

uint32_t matrixValue(const Display::PreparedFrame& frame, uint8_t slot)
{
    return (static_cast<uint32_t>(frameByte(frame, slot, kMatrixByte3)) << 16U) |
           (static_cast<uint32_t>(frameByte(frame, slot, kMatrixByte2)) << 8U) |
           static_cast<uint32_t>(frameByte(frame, slot, kMatrixByte1));
}

// Every byte of the frame other than the one at (slot, byte) is zero.
void expectOnlyByte(const Display::PreparedFrame& frame, uint8_t slot, uint8_t byte,
                    uint8_t expected, const std::string& caseName)
{
    for (uint8_t s = 0; s < Display::kSlotCount; ++s) {
        for (uint8_t b = 0; b < Display::kBytesPerSlot; ++b) {
            const uint8_t want = (s == slot && b == byte) ? expected : 0U;
            const std::string where = caseName + " slot " + std::to_string(s) + " byte " +
                                      std::to_string(b);
            expectEqual(frameByte(frame, s, b), want, where.c_str());
        }
    }
}

// A single normalized segment on one digit of one display lands on the
// documented SCT bit, in that display's byte of that digit's slot, and
// nowhere else.
void testSingleSegmentPerDisplay()
{
    for (uint8_t display = 0; display < Display::kNumericDisplayCount; ++display) {
        for (uint8_t slot = 0; slot < 4U; ++slot) {
            for (uint8_t segment = 0; segment < 8U; ++segment) {
                Display::LogicalBoardState state{};
                state.numeric[display].slots[slot] = static_cast<uint8_t>(1U << segment);
                Display::PreparedFrame frame{};
                Display::encodePcb(state, frame);
                expectOnlyByte(frame, slot, kNumericWireByte[display],
                               static_cast<uint8_t>(1U << kDocSegmentBit[segment][display]),
                               name("segment display/slot/segment", display, slot, segment));
            }
        }
    }
}

// Slot 4 is the special-indicator position: L1, L2 and L3 use the segment-A,
// -B and -C mappings.
void testIndicators()
{
    for (uint8_t display = 0; display < Display::kNumericDisplayCount; ++display) {
        Display::LogicalBoardState state{};
        state.numeric[display].slots[4] = static_cast<uint8_t>(kA | kB | kC);
        Display::PreparedFrame frame{};
        Display::encodePcb(state, frame);
        const uint8_t expected = static_cast<uint8_t>((1U << kDocSegmentBit[0][display]) |
                                                      (1U << kDocSegmentBit[1][display]) |
                                                      (1U << kDocSegmentBit[2][display]));
        expectOnlyByte(frame, 4U, kNumericWireByte[display], expected,
                       name("indicators display", display, 0, 0));
    }
}

// Hand-computed glyphs from the wiring table.
void testGlyphs()
{
    Display::LogicalBoardState state{};
    state.numeric[0].slots[0] = static_cast<uint8_t>(kB | kC);      // "1": bits 1, 4
    state.numeric[1].slots[1] = kG;                                 // "-": bit 4
    state.numeric[2].slots[2] = static_cast<uint8_t>(kA | kB | kC); // "7": bits 0, 1, 3
    state.numeric[3].slots[3] = static_cast<uint8_t>(kB | kC);      // "1": bits 1, 7
    state.numeric[0].slots[3] = 0xFFU;                              // "8." lights all
    Display::PreparedFrame frame{};
    Display::encodePcb(state, frame);

    expectEqual(frameByte(frame, 0U, 6U), 0x12U, "display 1 digit 1 '1'");
    expectEqual(frameByte(frame, 1U, 5U), 0x10U, "display 2 digit 2 '-'");
    expectEqual(frameByte(frame, 2U, 1U), 0x0BU, "display 3 digit 3 '7'");
    expectEqual(frameByte(frame, 3U, 0U), 0x82U, "display 4 digit 4 '1'");
    expectEqual(frameByte(frame, 3U, 6U), 0xFFU, "display 1 digit 4 '8.'");
}

// Column n (bit n-1 of a row) drives SCT bit n-1 of the matrix: bits 0-7 in
// matrix byte 1, the last byte on the wire, and bits 16-20 in matrix byte 3.
void testMatrixColumns()
{
    for (uint8_t column = 0; column < Display::kMatrixColumnCount; ++column) {
        Display::LogicalBoardState state{};
        state.matrix[4] = 1UL << column; // bottom row, slot 0
        Display::PreparedFrame frame{};
        Display::encodePcb(state, frame);
        const uint8_t byte = column < 8U ? kMatrixByte1 : column < 16U ? kMatrixByte2 : kMatrixByte3;
        expectOnlyByte(frame, 0U, byte, static_cast<uint8_t>(1U << (column % 8U)),
                       name("matrix column", column + 1U, 0, 0));
    }
}

void testMatrixUnusedBitsCleared()
{
    Display::LogicalBoardState state{};
    state.matrix = {0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU};
    Display::PreparedFrame frame{};
    Display::encodePcb(state, frame);
    for (uint8_t slot = 0; slot < Display::kSlotCount; ++slot) {
        expectEqual(frameByte(frame, slot, kMatrixByte3), 0x1FU, "bits 21-23 zero");
        expectEqual(frameByte(frame, slot, kMatrixByte2), 0xFFU, "columns 9-16");
        expectEqual(frameByte(frame, slot, kMatrixByte1), 0xFFU, "columns 1-8");
    }
}

// Slot k is driven by DISPLAY_(k+1)_EN. The encoder puts logical row 4 (the
// bottom) in slot 0 and row 0 (the top) in slot 4; docs/Display.md numbers
// the rows 1 to 5 by enable line without saying which end row 1 is.
void testMatrixRowsMapTopToBottom()
{
    Display::LogicalBoardState state{};
    state.matrix = {0x00001U, 0x00012U, 0x00123U, 0x01234U, 0x12345U};
    Display::PreparedFrame frame{};

    Display::encodePcb(state, frame);

    expectEqual(matrixValue(frame, 0U), state.matrix[4], "physical bottom row");
    expectEqual(matrixValue(frame, 1U), state.matrix[3], "physical row 1");
    expectEqual(matrixValue(frame, 2U), state.matrix[2], "physical row 2");
    expectEqual(matrixValue(frame, 3U), state.matrix[1], "physical row 3");
    expectEqual(matrixValue(frame, 4U), state.matrix[0], "physical top row");
}

void testEmptyStateEncodesToZero()
{
    Display::LogicalBoardState state{};
    Display::PreparedFrame frame{};
    frame.fill(0xAAU);
    Display::encodePcb(state, frame);
    for (const uint8_t byte : frame) {
        expectEqual(byte, 0U, "empty state clears the frame");
    }
}

} // namespace

int main()
{
    testSingleSegmentPerDisplay();
    testIndicators();
    testGlyphs();
    testMatrixColumns();
    testMatrixUnusedBitsCleared();
    testMatrixRowsMapTopToBottom();
    testEmptyStateEncodesToZero();
    return Test::finish("DisplayCodec");
}
